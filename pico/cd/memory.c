// Memory I/O handlers for Sega/Mega CD.
// (c) Copyright 2007-2009, Grazvydas "notaz" Ignotas

#include "../pico_int.h"
#include "../memory.h"

#include "gfx_cd.h"
#include "pcm.h"

unsigned long s68k_read8_map  [0x1000000 >> M68K_MEM_SHIFT];
unsigned long s68k_read16_map [0x1000000 >> M68K_MEM_SHIFT];
unsigned long s68k_write8_map [0x1000000 >> M68K_MEM_SHIFT];
unsigned long s68k_write16_map[0x1000000 >> M68K_MEM_SHIFT];

MAKE_68K_READ8(s68k_read8, s68k_read8_map)
MAKE_68K_READ16(s68k_read16, s68k_read16_map)
MAKE_68K_READ32(s68k_read32, s68k_read16_map)
MAKE_68K_WRITE8(s68k_write8, s68k_write8_map)
MAKE_68K_WRITE16(s68k_write16, s68k_write16_map)
MAKE_68K_WRITE32(s68k_write32, s68k_write16_map)

// -----------------------------------------------------------------

// poller detection
#define POLL_LIMIT 16
#define POLL_CYCLES 124
unsigned int s68k_poll_adclk, s68k_poll_cnt;

#ifndef _ASM_CD_MEMORY_C
static u32 m68k_reg_read16(u32 a)
{
  u32 d=0;
  a &= 0x3e;

  switch (a) {
    case 0:
      d = ((Pico_mcd->s68k_regs[0x33]<<13)&0x8000) | Pico_mcd->m.busreq; // here IFL2 is always 0, just like in Gens
      goto end;
    case 2:
      d = (Pico_mcd->s68k_regs[a]<<8) | (Pico_mcd->s68k_regs[a+1]&0xc7);
      elprintf(EL_CDREG3, "m68k_regs r3: %02x @%06x", (u8)d, SekPc);
      goto end;
    case 4:
      d = Pico_mcd->s68k_regs[4]<<8;
      goto end;
    case 6:
      d = *(u16 *)(Pico_mcd->bios + 0x72);
      goto end;
    case 8:
      d = Read_CDC_Host(0);
      goto end;
    case 0xA:
      elprintf(EL_UIO, "m68k FIXME: reserved read");
      goto end;
    case 0xC:
      d = Pico_mcd->m.timer_stopwatch >> 16;
      elprintf(EL_CDREGS, "m68k stopwatch timer read (%04x)", d);
      goto end;
  }

  if (a < 0x30) {
    // comm flag/cmd/status (0xE-0x2F)
    d = (Pico_mcd->s68k_regs[a]<<8) | Pico_mcd->s68k_regs[a+1];
    goto end;
  }

  elprintf(EL_UIO, "m68k_regs FIXME invalid read @ %02x", a);

end:

  return d;
}
#endif

#ifndef _ASM_CD_MEMORY_C
static
#endif
void m68k_reg_write8(u32 a, u32 d)
{
  u32 dold;
  a &= 0x3f;

  switch (a) {
    case 0:
      d &= 1;
      if ((d&1) && (Pico_mcd->s68k_regs[0x33]&(1<<2))) { elprintf(EL_INTS, "m68k: s68k irq 2"); SekInterruptS68k(2); }
      return;
    case 1:
      d &= 3;
      if (!(d&1)) Pico_mcd->m.state_flags |= 1; // reset pending, needed to be sure we fetch the right vectors on reset
      if ( (Pico_mcd->m.busreq&1) != (d&1)) elprintf(EL_INTSW, "m68k: s68k reset %i", !(d&1));
      if ( (Pico_mcd->m.busreq&2) != (d&2)) elprintf(EL_INTSW, "m68k: s68k brq %i", (d&2)>>1);
      if ((Pico_mcd->m.state_flags&1) && (d&3)==1) {
        SekResetS68k(); // S68k comes out of RESET or BRQ state
        Pico_mcd->m.state_flags&=~1;
        elprintf(EL_CDREGS, "m68k: resetting s68k, cycles=%i", SekCyclesLeft);
      }
      if (!(d & 1))
        d |= 2; // verified: reset also gives bus
      if ((d ^ Pico_mcd->m.busreq) & 2)
        PicoMemRemapCD(Pico_mcd->s68k_regs[3]);
      Pico_mcd->m.busreq = d;
      return;
    case 2:
      elprintf(EL_CDREGS, "m68k: prg wp=%02x", d);
      Pico_mcd->s68k_regs[2] = d; // really use s68k side register
      return;
    case 3:
      dold = Pico_mcd->s68k_regs[3];
      elprintf(EL_CDREG3, "m68k_regs w3: %02x @%06x", (u8)d, SekPc);
      //if ((Pico_mcd->s68k_regs[3]&4) != (d&4)) dprintf("m68k: ram mode %i mbit", (d&4) ? 1 : 2);
      //if ((Pico_mcd->s68k_regs[3]&2) != (d&2)) dprintf("m68k: %s", (d&4) ? ((d&2) ? "word swap req" : "noop?") :
      //                                             ((d&2) ? "word ram to s68k" : "word ram to m68k"));
      if (dold & 4) {   // 1M mode
        d ^= 2;         // writing 0 to DMNA actually sets it, 1 does nothing
      } else {
	if ((d ^ dold) & d & 2) { // DMNA is being set
          dold &= ~1;   // return word RAM to s68k
          /* Silpheed hack: bset(w3), r3, btst, bne, r3 */
          SekEndRun(20+16+10+12+16);
        }
      }
      Pico_mcd->s68k_regs[3] = (d & 0xc2) | (dold & 0x1f);
      if ((d ^ dold) & 0xc0) {
        elprintf(EL_CDREGS, "m68k: prg bank: %i -> %i", (Pico_mcd->s68k_regs[a]>>6), ((d>>6)&3));
        PicoMemRemapCD(Pico_mcd->s68k_regs[3]);
      }
#ifdef USE_POLL_DETECT
      if ((s68k_poll_adclk&0xfe) == 2 && s68k_poll_cnt > POLL_LIMIT) {
        SekSetStopS68k(0); s68k_poll_adclk = 0;
        elprintf(EL_CDPOLL, "s68k poll release, a=%02x", a);
      }
#endif
      return;
    case 6:
      Pico_mcd->bios[0x72 + 1] = d; // simple hint vector changer
      return;
    case 7:
      Pico_mcd->bios[0x72] = d;
      elprintf(EL_CDREGS, "hint vector set to %04x%04x",
        ((u16 *)Pico_mcd->bios)[0x70/2], ((u16 *)Pico_mcd->bios)[0x72/2]);
      return;
    case 0xf:
      d = (d << 1) | ((d >> 7) & 1); // rol8 1 (special case)
    case 0xe:
      //dprintf("m68k: comm flag: %02x", d);
      Pico_mcd->s68k_regs[0xe] = d;
#ifdef USE_POLL_DETECT
      if ((s68k_poll_adclk&0xfe) == 0xe && s68k_poll_cnt > POLL_LIMIT) {
        SekSetStopS68k(0); s68k_poll_adclk = 0;
        elprintf(EL_CDPOLL, "s68k poll release, a=%02x", a);
      }
#endif
      return;
  }

  if ((a&0xf0) == 0x10) {
      Pico_mcd->s68k_regs[a] = d;
#ifdef USE_POLL_DETECT
      if ((a&0xfe) == (s68k_poll_adclk&0xfe) && s68k_poll_cnt > POLL_LIMIT) {
        SekSetStopS68k(0); s68k_poll_adclk = 0;
        elprintf(EL_CDPOLL, "s68k poll release, a=%02x", a);
      }
#endif
      return;
  }

  elprintf(EL_UIO, "m68k FIXME: invalid write? [%02x] %02x", a, d);
}

#ifndef _ASM_CD_MEMORY_C
static
#endif
u32 s68k_poll_detect(u32 a, u32 d)
{
#ifdef USE_POLL_DETECT
  // needed mostly for Cyclone, which doesn't always check it's cycle counter
  if (SekIsStoppedS68k()) return d;
  // polling detection
  if (a == (s68k_poll_adclk&0xff)) {
    unsigned int clkdiff = SekCyclesDoneS68k() - (s68k_poll_adclk>>8);
    if (clkdiff <= POLL_CYCLES) {
      s68k_poll_cnt++;
      //printf("-- diff: %u, cnt = %i\n", clkdiff, s68k_poll_cnt);
      if (s68k_poll_cnt > POLL_LIMIT) {
        SekSetStopS68k(1);
        elprintf(EL_CDPOLL, "s68k poll detected @ %06x, a=%02x", SekPcS68k, a);
      }
      s68k_poll_adclk = (SekCyclesDoneS68k() << 8) | a;
      return d;
    }
  }
  s68k_poll_adclk = (SekCyclesDoneS68k() << 8) | a;
  s68k_poll_cnt = 0;
#endif
  return d;
}

#define READ_FONT_DATA(basemask) \
{ \
      unsigned int fnt = *(unsigned int *)(Pico_mcd->s68k_regs + 0x4c); \
      unsigned int col0 = (fnt >> 8) & 0x0f, col1 = (fnt >> 12) & 0x0f;   \
      if (fnt & (basemask << 0)) d  = col1      ; else d  = col0;       \
      if (fnt & (basemask << 1)) d |= col1 <<  4; else d |= col0 <<  4; \
      if (fnt & (basemask << 2)) d |= col1 <<  8; else d |= col0 <<  8; \
      if (fnt & (basemask << 3)) d |= col1 << 12; else d |= col0 << 12; \
}


#ifndef _ASM_CD_MEMORY_C
static
#endif
u32 s68k_reg_read16(u32 a)
{
  u32 d=0;

  switch (a) {
    case 0:
      return ((Pico_mcd->s68k_regs[0]&3)<<8) | 1; // ver = 0, not in reset state
    case 2:
      d = (Pico_mcd->s68k_regs[2]<<8) | (Pico_mcd->s68k_regs[3]&0x1f);
      elprintf(EL_CDREG3, "s68k_regs r3: %02x @%06x", (u8)d, SekPcS68k);
      return s68k_poll_detect(a, d);
    case 6:
      return CDC_Read_Reg();
    case 8:
      return Read_CDC_Host(1); // Gens returns 0 here on byte reads
    case 0xC:
      d = Pico_mcd->m.timer_stopwatch >> 16;
      elprintf(EL_CDREGS, "s68k stopwatch timer read (%04x)", d);
      return d;
    case 0x30:
      elprintf(EL_CDREGS, "s68k int3 timer read (%02x)", Pico_mcd->s68k_regs[31]);
      return Pico_mcd->s68k_regs[31];
    case 0x34: // fader
      return 0; // no busy bit
    case 0x50: // font data (check: Lunar 2, Silpheed)
      READ_FONT_DATA(0x00100000);
      return d;
    case 0x52:
      READ_FONT_DATA(0x00010000);
      return d;
    case 0x54:
      READ_FONT_DATA(0x10000000);
      return d;
    case 0x56:
      READ_FONT_DATA(0x01000000);
      return d;
  }

  d = (Pico_mcd->s68k_regs[a]<<8) | Pico_mcd->s68k_regs[a+1];

  if (a >= 0x0e && a < 0x30)
    return s68k_poll_detect(a, d);

  return d;
}

#ifndef _ASM_CD_MEMORY_C
static
#endif
void s68k_reg_write8(u32 a, u32 d)
{
  // Warning: d might have upper bits set
  switch (a) {
    case 2:
      return; // only m68k can change WP
    case 3: {
      int dold = Pico_mcd->s68k_regs[3];
      elprintf(EL_CDREG3, "s68k_regs w3: %02x @%06x", (u8)d, SekPcS68k);
      d &= 0x1d;
      d |= dold & 0xc2;
      if (d & 4)
      {
        if ((d ^ dold) & 5) {
          d &= ~2; // in case of mode or bank change we clear DMNA (m68k req) bit
          PicoMemRemapCD(d);
        }
#ifdef _ASM_CD_MEMORY_C
        if ((d ^ dold) & 0x1d)
          PicoMemResetCDdecode(d);
#endif
        if (!(dold & 4)) {
          elprintf(EL_CDREG3, "wram mode 2M->1M");
          wram_2M_to_1M(Pico_mcd->word_ram2M);
        }
      }
      else
      {
        if (dold & 4) {
          elprintf(EL_CDREG3, "wram mode 1M->2M");
          if (!(d&1)) { // it didn't set the ret bit, which means it doesn't want to give WRAM to m68k
            d &= ~3;
            d |= (dold&1) ? 2 : 1; // then give it to the one which had bank0 in 1M mode
          }
          wram_1M_to_2M(Pico_mcd->word_ram2M);
          PicoMemRemapCD(d);
        }
        // s68k can only set RET, writing 0 has no effect
        else if ((dold ^ d) & d & 1) {   // RET being set
          SekEndRunS68k(20+16+10+12+16); // see DMNA case
        } else
          d |= dold & 1;
        if (d & 1)
          d &= ~2;                       // DMNA clears
      }
      break;
    }
    case 4:
      elprintf(EL_CDREGS, "s68k CDC dest: %x", d&7);
      Pico_mcd->s68k_regs[4] = (Pico_mcd->s68k_regs[4]&0xC0) | (d&7); // CDC mode
      return;
    case 5:
      //dprintf("s68k CDC reg addr: %x", d&0xf);
      break;
    case 7:
      CDC_Write_Reg(d);
      return;
    case 0xa:
      elprintf(EL_CDREGS, "s68k set CDC dma addr");
      break;
    case 0xc:
    case 0xd:
      elprintf(EL_CDREGS, "s68k set stopwatch timer");
      Pico_mcd->m.timer_stopwatch = 0;
      return;
    case 0xe:
      Pico_mcd->s68k_regs[0xf] = (d>>1) | (d<<7); // ror8 1, Gens note: Dragons lair
      return;
    case 0x31:
      elprintf(EL_CDREGS, "s68k set int3 timer: %02x", d);
      Pico_mcd->m.timer_int3 = (d & 0xff) << 16;
      break;
    case 0x33: // IRQ mask
      elprintf(EL_CDREGS, "s68k irq mask: %02x", d);
      if ((d&(1<<4)) && (Pico_mcd->s68k_regs[0x37]&4) && !(Pico_mcd->s68k_regs[0x33]&(1<<4))) {
        CDD_Export_Status();
      }
      break;
    case 0x34: // fader
      Pico_mcd->s68k_regs[a] = (u8) d & 0x7f;
      return;
    case 0x36:
      return; // d/m bit is unsetable
    case 0x37: {
      u32 d_old = Pico_mcd->s68k_regs[0x37];
      Pico_mcd->s68k_regs[0x37] = d&7;
      if ((d&4) && !(d_old&4)) {
        CDD_Export_Status();
      }
      return;
    }
    case 0x4b:
      Pico_mcd->s68k_regs[a] = (u8) d;
      CDD_Import_Command();
      return;
  }

  if ((a&0x1f0) == 0x10 || (a >= 0x38 && a < 0x42))
  {
    elprintf(EL_UIO, "s68k FIXME: invalid write @ %02x?", a);
    return;
  }

  Pico_mcd->s68k_regs[a] = (u8) d;
}

// -----------------------------------------------------------------
//                          Main 68k
// -----------------------------------------------------------------

#ifndef _ASM_CD_MEMORY_C
#include "cell_map.c"
#endif

// WORD RAM, cell aranged area (220000 - 23ffff)
static u32 PicoReadM68k8_cell(u32 a)
{
  int bank = Pico_mcd->s68k_regs[3] & 1;
  a = (a&3) | (cell_map(a >> 2) << 2); // cell arranged
  return Pico_mcd->word_ram1M[bank][a ^ 1];
}

static u32 PicoReadM68k16_cell(u32 a)
{
  int bank = Pico_mcd->s68k_regs[3] & 1;
  a = (a&2) | (cell_map(a >> 2) << 2);
  return *(u16 *)(Pico_mcd->word_ram1M[bank] + a);
}

static void PicoWriteM68k8_cell(u32 a, u32 d)
{
  int bank = Pico_mcd->s68k_regs[3] & 1;
  a = (a&3) | (cell_map(a >> 2) << 2);
  Pico_mcd->word_ram1M[bank][a ^ 1] = d;
}

static void PicoWriteM68k16_cell(u32 a, u32 d)
{
  int bank = Pico_mcd->s68k_regs[3] & 1;
  a = (a&3) | (cell_map(a >> 2) << 2);
  *(u16 *)(Pico_mcd->word_ram1M[bank] + a) = d;
}

// RAM cart (40000 - 7fffff, optional)
static u32 PicoReadM68k8_ramc(u32 a)
{
  u32 d = 0;
  if (a == 0x400001) {
    if (SRam.data != NULL)
      d = 3; // 64k cart
    return d;
  }

  if ((a & 0xfe0000) == 0x600000) {
    if (SRam.data != NULL)
      d = SRam.data[((a >> 1) & 0xffff) + 0x2000];
    return d;
  }

  if (a == 0x7fffff)
    return Pico_mcd->m.bcram_reg;

  elprintf(EL_UIO, "m68k unmapped r8  [%06x] @%06x", a, SekPc);
  return d;
}

static u32 PicoReadM68k16_ramc(u32 a)
{
  elprintf(EL_ANOMALY, "ramcart r16: [%06x] @%06x", a, SekPcS68k);
  return PicoReadM68k8_ramc(a + 1);
}

static void PicoWriteM68k8_ramc(u32 a, u32 d)
{
  if ((a & 0xfe0000) == 0x600000) {
    if (SRam.data != NULL && (Pico_mcd->m.bcram_reg & 1)) {
      SRam.data[((a>>1) & 0xffff) + 0x2000] = d;
      SRam.changed = 1;
    }
    return;
  }

  if (a == 0x7fffff) {
    Pico_mcd->m.bcram_reg = d;
    return;
  }

  elprintf(EL_UIO, "m68k unmapped w8  [%06x]   %02x @%06x", a, d & 0xff, SekPc);
}

static void PicoWriteM68k16_ramc(u32 a, u32 d)
{
  elprintf(EL_ANOMALY, "ramcart w16: [%06x] %04x @%06x", a, d, SekPcS68k);
  PicoWriteM68k8_ramc(a + 1, d);
}

// IO/control/cd registers (a10000 - ...)
static u32 PicoReadM68k8_io(u32 a)
{
  u32 d;
  if ((a & 0xff00) == 0x2000) { // a12000 - a120ff
    d = m68k_reg_read16(a); // TODO: m68k_reg_read8
    if (!(a & 1))
      d >>= 8;
    d &= 0xff;
    elprintf(EL_CDREGS, "m68k_regs r8:  [%02x]   %02x @%06x", a & 0x3f, d, SekPc);
    return d;
  }

  // fallback to default MD handler
  return PicoRead8_io(a);
}

static u32 PicoReadM68k16_io(u32 a)
{
  u32 d;
  if ((a & 0xff00) == 0x2000) {
    d = m68k_reg_read16(a);
    elprintf(EL_CDREGS, "m68k_regs r16: [%02x] %04x @%06x", a & 0x3f, d, SekPc);
    return d;
  }

  return PicoRead16_io(a);
}

static void PicoWriteM68k8_io(u32 a, u32 d)
{
  if ((a & 0xff00) == 0x2000) { // a12000 - a120ff
    elprintf(EL_CDREGS, "m68k_regs w8:  [%02x]   %02x @%06x", a&0x3f, d, SekPc);
    m68k_reg_write8(a, d);
    return;
  }

  PicoWrite16_io(a, d);
}

static void PicoWriteM68k16_io(u32 a, u32 d)
{
  if ((a & 0xff00) == 0x2000) { // a12000 - a120ff
    elprintf(EL_CDREGS, "m68k_regs w16: [%02x] %04x @%06x", a&0x3f, d, SekPc);
/* TODO FIXME?
    if (a == 0xe) { // special case, 2 byte writes would be handled differently
      Pico_mcd->s68k_regs[0xe] = d >> 8;
#ifdef USE_POLL_DETECT
      if ((s68k_poll_adclk&0xfe) == 0xe && s68k_poll_cnt > POLL_LIMIT) {
        SekSetStopS68k(0); s68k_poll_adclk = 0;
        elprintf(EL_CDPOLL, "s68k poll release, a=%02x", a);
      }
#endif
      return;
    }
*/
    m68k_reg_write8(a,     d >> 8);
    m68k_reg_write8(a + 1, d & 0xff);
    return;
  }

  PicoWrite16_io(a, d);
}

// -----------------------------------------------------------------
//                           Sub 68k
// -----------------------------------------------------------------

static u32 s68k_unmapped_read8(u32 a)
{
  elprintf(EL_UIO, "s68k unmapped r8  [%06x] @%06x", a, SekPc);
  return 0;
}

static u32 s68k_unmapped_read16(u32 a)
{
  elprintf(EL_UIO, "s68k unmapped r16 [%06x] @%06x", a, SekPc);
  return 0;
}

static void s68k_unmapped_write8(u32 a, u32 d)
{
  elprintf(EL_UIO, "s68k unmapped w8  [%06x]   %02x @%06x", a, d & 0xff, SekPc);
}

static void s68k_unmapped_write16(u32 a, u32 d)
{
  elprintf(EL_UIO, "s68k unmapped w16 [%06x] %04x @%06x", a, d & 0xffff, SekPc);
}

// decode (080000 - 0bffff, in 1M mode)
static u32 PicoReadS68k8_dec(u32 a)
{
  u32 d, bank;
  bank = (Pico_mcd->s68k_regs[3] & 1) ^ 1;
  d = Pico_mcd->word_ram1M[bank][((a >> 1) ^ 1) & 0x1ffff];
  if (a & 1)
    d &= 0x0f;
  else
    d >>= 4;
  return d;
}

static u32 PicoReadS68k16_dec(u32 a)
{
  u32 d, bank;
  bank = (Pico_mcd->s68k_regs[3] & 1) ^ 1;
  d = Pico_mcd->word_ram1M[bank][((a >> 1) ^ 1) & 0x1ffff];
  d |= d << 4;
  d &= ~0xf0;
  return d;
}

/* check: jaguar xj 220 (draws entire world using decode) */
static void PicoWriteS68k8_dec(u32 a, u32 d)
{
  u8 r3 = Pico_mcd->s68k_regs[3];
  u8 *pd = &Pico_mcd->word_ram1M[(r3 & 1) ^ 1][((a >> 1) ^ 1) & 0x1ffff];
  u8 oldmask = (a & 1) ? 0xf0 : 0x0f;

  r3 &= 0x18;
  d  &= 0x0f;
  if (!(a & 1))
    d <<= 4;

  if (r3 == 8) {
    if ((!(*pd & (~oldmask))) && d)
      goto do_it;
  } else if (r3 > 8) {
    if (d)
      goto do_it;
  } else
    goto do_it;

  return;

do_it:
  *pd = d | (*pd & oldmask);
}

static void PicoWriteS68k16_dec(u32 a, u32 d)
{
  u8 r3 = Pico_mcd->s68k_regs[3];
  u8 *pd = &Pico_mcd->word_ram1M[(r3 & 1) ^ 1][((a >> 1) ^ 1) & 0x1ffff];

  //if ((a & 0x3ffff) < 0x28000) return;

  r3 &= 0x18;
  d  &= 0x0f0f;
  d  |= d >> 4;

  if (r3 == 8) {
    u8 dold = *pd;
    if (!(dold & 0xf0)) dold |= d & 0xf0;
    if (!(dold & 0x0f)) dold |= d & 0x0f;
    *pd = dold;
  } else if (r3 > 8) {
    u8 dold = *pd;
    if (!(d & 0xf0)) d |= dold & 0xf0;
    if (!(d & 0x0f)) d |= dold & 0x0f;
    *pd = d;
  } else {
    *pd = d;
  }
}

// backup RAM (fe0000 - feffff)
static u32 PicoReadS68k8_bram(u32 a)
{
  return Pico_mcd->bram[(a>>1)&0x1fff];
}

static u32 PicoReadS68k16_bram(u32 a)
{
  u32 d;
  elprintf(EL_ANOMALY, "FIXME: s68k_bram r16: [%06x] @%06x", a, SekPcS68k);
  a = (a >> 1) & 0x1fff;
  d = Pico_mcd->bram[a++];
  d|= Pico_mcd->bram[a++] << 8; // probably wrong, TODO: verify
  return d;
}

static void PicoWriteS68k8_bram(u32 a, u32 d)
{
  Pico_mcd->bram[(a >> 1) & 0x1fff] = d;
  SRam.changed = 1;
}

static void PicoWriteS68k16_bram(u32 a, u32 d)
{
  elprintf(EL_ANOMALY, "s68k_bram w16: [%06x] %04x @%06x", a, d, SekPcS68k);
  a = (a >> 1) & 0x1fff;
  Pico_mcd->bram[a++] = d;
  Pico_mcd->bram[a++] = d >> 8; // TODO: verify..
  SRam.changed = 1;
}

// PCM and registers (ff0000 - ffffff)
static u32 PicoReadS68k8_pr(u32 a)
{
  u32 d = 0;

  // regs
  if ((a & 0xfe00) == 0x8000) {
    a &= 0x1ff;
    elprintf(EL_CDREGS, "s68k_regs r8: [%02x] @ %06x", a, SekPcS68k);
    if (a >= 0x0e && a < 0x30) {
      d = Pico_mcd->s68k_regs[a];
      s68k_poll_detect(a, d);
      elprintf(EL_CDREGS, "ret = %02x", (u8)d);
      return d;
    }
    else if (a >= 0x58 && a < 0x68)
         d = gfx_cd_read(a & ~1);
    else d = s68k_reg_read16(a & ~1);
    if (!(a & 1))
      d >>= 8;
    elprintf(EL_CDREGS, "ret = %02x", (u8)d);
    return d & 0xff;
  }

  // PCM
  if ((a & 0x8000) == 0x0000) {
    a &= 0x7fff;
    if (a >= 0x2000)
      d = Pico_mcd->pcm_ram_b[Pico_mcd->pcm.bank][(a >> 1) & 0xfff];
    else if (a >= 0x20) {
      a &= 0x1e;
      d = Pico_mcd->pcm.ch[a>>2].addr >> PCM_STEP_SHIFT;
      if (a & 2)
        d >>= 8;
    }
    return d & 0xff;
  }

  return s68k_unmapped_read8(a);
}

static u32 PicoReadS68k16_pr(u32 a)
{
  u32 d = 0;

  // regs
  if ((a & 0xfe00) == 0x8000) {
    a &= 0x1fe;
    elprintf(EL_CDREGS, "s68k_regs r16: [%02x] @ %06x", a, SekPcS68k);
    if (0x58 <= a && a < 0x68)
         d = gfx_cd_read(a);
    else d = s68k_reg_read16(a);
    elprintf(EL_CDREGS, "ret = %04x", d);
    return d;
  }

  // PCM
  if ((a & 0x8000) == 0x0000) {
    //elprintf(EL_ANOMALY, "FIXME: s68k_pcm r16: [%06x] @%06x", a, SekPcS68k);
    a &= 0x7fff;
    if (a >= 0x2000)
      d = Pico_mcd->pcm_ram_b[Pico_mcd->pcm.bank][(a>>1)&0xfff];
    else if (a >= 0x20) {
      a &= 0x1e;
      d = Pico_mcd->pcm.ch[a>>2].addr >> PCM_STEP_SHIFT;
      if (a & 2) d >>= 8;
    }
    elprintf(EL_CDREGS, "ret = %04x", d);
    return d;
  }

  return s68k_unmapped_read16(a);
}

static void PicoWriteS68k8_pr(u32 a, u32 d)
{
  // regs
  if ((a & 0xfe00) == 0x8000) {
    a &= 0x1ff;
    elprintf(EL_CDREGS, "s68k_regs w8: [%02x] %02x @ %06x", a, d, SekPcS68k);
    if (0x58 <= a && a < 0x68)
         gfx_cd_write16(a&~1, (d<<8)|d);
    else s68k_reg_write8(a,d);
    return;
  }

  // PCM
  if ((a & 0x8000) == 0x0000) {
    a &= 0x7fff;
    if (a >= 0x2000)
      Pico_mcd->pcm_ram_b[Pico_mcd->pcm.bank][(a>>1)&0xfff] = d;
    else if (a < 0x12)
      pcm_write(a>>1, d);
    return;
  }

  s68k_unmapped_write8(a, d);
}

static void PicoWriteS68k16_pr(u32 a, u32 d)
{
  // regs
  if ((a & 0xfe00) == 0x8000) {
    a &= 0x1fe;
    elprintf(EL_CDREGS, "s68k_regs w16: [%02x] %04x @ %06x", a, d, SekPcS68k);
    if (a >= 0x58 && a < 0x68)
      gfx_cd_write16(a, d);
    else {
      if (a == 0xe) {
        // special case, 2 byte writes would be handled differently
        // TODO: verify
        Pico_mcd->s68k_regs[0xf] = d;
        return;
      }
      s68k_reg_write8(a,     d >> 8);
      s68k_reg_write8(a + 1, d & 0xff);
    }
    return;
  }

  // PCM
  if ((a & 0x8000) == 0x0000) {
    a &= 0x7fff;
    if (a >= 0x2000)
      Pico_mcd->pcm_ram_b[Pico_mcd->pcm.bank][(a>>1)&0xfff] = d;
    else if (a < 0x12)
      pcm_write(a>>1, d & 0xff);
    return;
  }

  s68k_unmapped_write16(a, d);
}

// -----------------------------------------------------------------

// TODO: probably split
void PicoMemRemapCD(int r3)
{
  void *bank;

  // PRG RAM
  if (Pico_mcd->m.busreq & 2) {
    bank = Pico_mcd->prg_ram_b[Pico_mcd->s68k_regs[3] >> 6];
    cpu68k_map_all_ram(0x020000, 0x03ffff, bank, 0);
  }
  else {
    m68k_map_unmap(0x020000, 0x03ffff);
  }

  // WORD RAM
  if (!(r3 & 4)) {
    // 2M mode. XXX: allowing access in all cases for simplicity
    bank = Pico_mcd->word_ram2M;
    cpu68k_map_all_ram(0x200000, 0x23ffff, bank, 0);
    cpu68k_map_all_ram(0x080000, 0x0bffff, bank, 1);
    // TODO: handle 0x0c0000
  }
  else {
    bank = Pico_mcd->word_ram1M[r3 & 1];
    cpu68k_map_all_ram(0x200000, 0x21ffff, bank, 0);
    bank = Pico_mcd->word_ram1M[(r3 & 1) ^ 1];
    cpu68k_map_all_ram(0x0c0000, 0x0effff, bank, 1);
    // "cell arrange" on m68k
    cpu68k_map_set(m68k_read8_map,   0x220000, 0x23ffff, PicoReadM68k8_cell, 1);
    cpu68k_map_set(m68k_read16_map,  0x220000, 0x23ffff, PicoReadM68k16_cell, 1);
    cpu68k_map_set(m68k_write8_map,  0x220000, 0x23ffff, PicoWriteM68k8_cell, 1);
    cpu68k_map_set(m68k_write16_map, 0x220000, 0x23ffff, PicoWriteM68k16_cell, 1);
    // "decode format" on s68k
    cpu68k_map_set(s68k_read8_map,   0x080000, 0x0bffff, PicoReadS68k8_dec, 1);
    cpu68k_map_set(s68k_read16_map,  0x080000, 0x0bffff, PicoReadS68k16_dec, 1);
    cpu68k_map_set(s68k_write8_map,  0x080000, 0x0bffff, PicoWriteS68k8_dec, 1);
    cpu68k_map_set(s68k_write16_map, 0x080000, 0x0bffff, PicoWriteS68k16_dec, 1);
  }

#ifdef EMU_F68K
  // update fetchmap..
  int i;
  if (!(r3 & 4))
  {
    for (i = M68K_FETCHBANK1*2/16; (i<<(24-FAMEC_FETCHBITS)) < 0x240000; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico_mcd->word_ram2M - 0x200000;
  }
  else
  {
    for (i = M68K_FETCHBANK1*2/16; (i<<(24-FAMEC_FETCHBITS)) < 0x220000; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico_mcd->word_ram1M[r3 & 1] - 0x200000;
    for (i = M68K_FETCHBANK1*0x0c/0x100; (i<<(24-FAMEC_FETCHBITS)) < 0x0e0000; i++)
      PicoCpuFS68k.Fetch[i] = (unsigned int)Pico_mcd->word_ram1M[(r3&1)^1] - 0x0c0000;
  }
#endif
}

#ifdef EMU_M68K
static void m68k_mem_setup_cd(void);
#endif

PICO_INTERNAL void PicoMemSetupCD(void)
{
  // setup default main68k map
  PicoMemSetup();

  // PicoMemRemapCD() will set up RAMs, so not done here

  // main68k map (BIOS mapped by PicoMemSetup()):
  // RAM cart
  if (PicoOpt & POPT_EN_MCD_RAMCART) {
    cpu68k_map_set(m68k_read8_map,   0x400000, 0x7fffff, PicoReadM68k8_ramc, 1);
    cpu68k_map_set(m68k_read16_map,  0x400000, 0x7fffff, PicoReadM68k16_ramc, 1);
    cpu68k_map_set(m68k_write8_map,  0x400000, 0x7fffff, PicoWriteM68k8_ramc, 1);
    cpu68k_map_set(m68k_write16_map, 0x400000, 0x7fffff, PicoWriteM68k16_ramc, 1);
  }

  // registers/IO:
  cpu68k_map_set(m68k_read8_map,   0xa10000, 0xa1ffff, PicoReadM68k8_io, 1);
  cpu68k_map_set(m68k_read16_map,  0xa10000, 0xa1ffff, PicoReadM68k16_io, 1);
  cpu68k_map_set(m68k_write8_map,  0xa10000, 0xa1ffff, PicoWriteM68k8_io, 1);
  cpu68k_map_set(m68k_write16_map, 0xa10000, 0xa1ffff, PicoWriteM68k16_io, 1);

  // sub68k map
  cpu68k_map_set(s68k_read8_map,   0x000000, 0xffffff, s68k_unmapped_read8, 1);
  cpu68k_map_set(s68k_read16_map,  0x000000, 0xffffff, s68k_unmapped_read16, 1);
  cpu68k_map_set(s68k_write8_map,  0x000000, 0xffffff, s68k_unmapped_write8, 1);
  cpu68k_map_set(s68k_write16_map, 0x000000, 0xffffff, s68k_unmapped_write16, 1);

  // PRG RAM
  cpu68k_map_set(s68k_read8_map,   0x000000, 0x07ffff, Pico_mcd->prg_ram, 0);
  cpu68k_map_set(s68k_read16_map,  0x000000, 0x07ffff, Pico_mcd->prg_ram, 0);
  cpu68k_map_set(s68k_write8_map,  0x000000, 0x07ffff, Pico_mcd->prg_ram, 0);
  cpu68k_map_set(s68k_write16_map, 0x000000, 0x07ffff, Pico_mcd->prg_ram, 0);

  // BRAM
  cpu68k_map_set(s68k_read8_map,   0xfe0000, 0xfeffff, PicoReadS68k8_bram, 1);
  cpu68k_map_set(s68k_read16_map,  0xfe0000, 0xfeffff, PicoReadS68k16_bram, 1);
  cpu68k_map_set(s68k_write8_map,  0xfe0000, 0xfeffff, PicoWriteS68k8_bram, 1);
  cpu68k_map_set(s68k_write16_map, 0xfe0000, 0xfeffff, PicoWriteS68k16_bram, 1);

  // PCM, regs
  cpu68k_map_set(s68k_read8_map,   0xff0000, 0xffffff, PicoReadS68k8_pr, 1);
  cpu68k_map_set(s68k_read16_map,  0xff0000, 0xffffff, PicoReadS68k16_pr, 1);
  cpu68k_map_set(s68k_write8_map,  0xff0000, 0xffffff, PicoWriteS68k8_pr, 1);
  cpu68k_map_set(s68k_write16_map, 0xff0000, 0xffffff, PicoWriteS68k16_pr, 1);

#ifdef EMU_C68K
  // s68k
  PicoCpuCS68k.read8  = (void *)s68k_read8_map;
  PicoCpuCS68k.read16 = (void *)s68k_read16_map;
  PicoCpuCS68k.read32 = (void *)s68k_read16_map;
  PicoCpuCS68k.write8  = (void *)s68k_write8_map;
  PicoCpuCS68k.write16 = (void *)s68k_write16_map;
  PicoCpuCS68k.write32 = (void *)s68k_write16_map;
  PicoCpuCS68k.checkpc = NULL; /* unused */
  PicoCpuCS68k.fetch8  = NULL;
  PicoCpuCS68k.fetch16 = NULL;
  PicoCpuCS68k.fetch32 = NULL;
#endif
#ifdef EMU_F68K
  // s68k
  PicoCpuFS68k.read_byte  = s68k_read8;
  PicoCpuFS68k.read_word  = s68k_read16;
  PicoCpuFS68k.read_long  = s68k_read32;
  PicoCpuFS68k.write_byte = s68k_write8;
  PicoCpuFS68k.write_word = s68k_write16;
  PicoCpuFS68k.write_long = s68k_write32;

  // setup FAME fetchmap
  {
    int i;
    // M68k
    // by default, point everything to fitst 64k of ROM (BIOS)
    for (i = 0; i < M68K_FETCHBANK1; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.rom - (i<<(24-FAMEC_FETCHBITS));
    // now real ROM (BIOS)
    for (i = 0; i < M68K_FETCHBANK1 && (i<<(24-FAMEC_FETCHBITS)) < Pico.romsize; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.rom;
    // .. and RAM
    for (i = M68K_FETCHBANK1*14/16; i < M68K_FETCHBANK1; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.ram - (i<<(24-FAMEC_FETCHBITS));
    // S68k
    // PRG RAM is default
    for (i = 0; i < M68K_FETCHBANK1; i++)
      PicoCpuFS68k.Fetch[i] = (unsigned int)Pico_mcd->prg_ram - (i<<(24-FAMEC_FETCHBITS));
    // real PRG RAM
    for (i = 0; i < M68K_FETCHBANK1 && (i<<(24-FAMEC_FETCHBITS)) < 0x80000; i++)
      PicoCpuFS68k.Fetch[i] = (unsigned int)Pico_mcd->prg_ram;
    // WORD RAM 2M area
    for (i = M68K_FETCHBANK1*0x08/0x100; i < M68K_FETCHBANK1 && (i<<(24-FAMEC_FETCHBITS)) < 0xc0000; i++)
      PicoCpuFS68k.Fetch[i] = (unsigned int)Pico_mcd->word_ram2M - 0x80000;
    // PicoMemRemapCD() will setup word ram for both
  }
#endif
#ifdef EMU_M68K
  m68k_mem_setup_cd();
#endif

  // m68k_poll_addr = m68k_poll_cnt = 0;
  s68k_poll_adclk = s68k_poll_cnt = 0;
}


#ifdef EMU_M68K
u32  m68k_read8(u32 a);
u32  m68k_read16(u32 a);
u32  m68k_read32(u32 a);
void m68k_write8(u32 a, u8 d);
void m68k_write16(u32 a, u16 d);
void m68k_write32(u32 a, u32 d);

static unsigned int PicoReadCD8w (unsigned int a) {
	return m68ki_cpu_p == &PicoCpuMS68k ? s68k_read8(a) : m68k_read8(a);
}
static unsigned int PicoReadCD16w(unsigned int a) {
	return m68ki_cpu_p == &PicoCpuMS68k ? s68k_read16(a) : m68k_read16(a);
}
static unsigned int PicoReadCD32w(unsigned int a) {
	return m68ki_cpu_p == &PicoCpuMS68k ? s68k_read32(a) : m68k_read32(a);
}
static void PicoWriteCD8w (unsigned int a, unsigned char d) {
	if (m68ki_cpu_p == &PicoCpuMS68k) s68k_write8(a, d); else m68k_write8(a, d);
}
static void PicoWriteCD16w(unsigned int a, unsigned short d) {
	if (m68ki_cpu_p == &PicoCpuMS68k) s68k_write16(a, d); else m68k_write16(a, d);
}
static void PicoWriteCD32w(unsigned int a, unsigned int d) {
	if (m68ki_cpu_p == &PicoCpuMS68k) s68k_write32(a, d); else m68k_write32(a, d);
}

extern unsigned int (*pm68k_read_memory_8) (unsigned int address);
extern unsigned int (*pm68k_read_memory_16)(unsigned int address);
extern unsigned int (*pm68k_read_memory_32)(unsigned int address);
extern void (*pm68k_write_memory_8) (unsigned int address, unsigned char  value);
extern void (*pm68k_write_memory_16)(unsigned int address, unsigned short value);
extern void (*pm68k_write_memory_32)(unsigned int address, unsigned int   value);

static void m68k_mem_setup_cd(void)
{
  pm68k_read_memory_8  = PicoReadCD8w;
  pm68k_read_memory_16 = PicoReadCD16w;
  pm68k_read_memory_32 = PicoReadCD32w;
  pm68k_write_memory_8  = PicoWriteCD8w;
  pm68k_write_memory_16 = PicoWriteCD16w;
  pm68k_write_memory_32 = PicoWriteCD32w;
}
#endif // EMU_M68K

