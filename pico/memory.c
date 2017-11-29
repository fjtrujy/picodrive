// This is part of Pico Library

// (c) Copyright 2004 Dave, All rights reserved.
// (c) Copyright 2006-2009 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.


#include "pico_int.h"
#include "memory.h"

#include "sound/ym2612.h"
#include "sound/sn76496.h"

extern unsigned int lastSSRamWrite; // used by serial eeprom code

unsigned long m68k_read8_map  [0x1000000 >> M68K_MEM_SHIFT];
unsigned long m68k_read16_map [0x1000000 >> M68K_MEM_SHIFT];
unsigned long m68k_write8_map [0x1000000 >> M68K_MEM_SHIFT];
unsigned long m68k_write16_map[0x1000000 >> M68K_MEM_SHIFT];

static void xmap_set(unsigned long *map, int shift, int start_addr, int end_addr,
    void *func_or_mh, int is_func)
{
  unsigned long addr = (unsigned long)func_or_mh;
  int mask = (1 << shift) - 1;
  int i;

  if ((start_addr & mask) != 0 || (end_addr & mask) != mask) {
    elprintf(EL_STATUS|EL_ANOMALY, "xmap_set: tried to map bad range: %06x-%06x",
      start_addr, end_addr);
    return;
  }

  if (addr & 1) {
    elprintf(EL_STATUS|EL_ANOMALY, "xmap_set: ptr is not aligned: %08lx", addr);
    return;
  }

  if (!is_func)
    addr -= start_addr;

  for (i = start_addr >> shift; i <= end_addr >> shift; i++) {
    map[i] = addr >> 1;
    if (is_func)
      map[i] |= 1 << (sizeof(addr) * 8 - 1);
  }
}

void z80_map_set(unsigned long *map, int start_addr, int end_addr,
    void *func_or_mh, int is_func)
{
  xmap_set(map, Z80_MEM_SHIFT, start_addr, end_addr, func_or_mh, is_func);
}

void cpu68k_map_set(unsigned long *map, int start_addr, int end_addr,
    void *func_or_mh, int is_func)
{
  xmap_set(map, M68K_MEM_SHIFT, start_addr, end_addr, func_or_mh, is_func);
}

// more specialized/optimized function (does same as above)
void cpu68k_map_all_ram(int start_addr, int end_addr, void *ptr, int is_sub)
{
  unsigned long *r8map, *r16map, *w8map, *w16map;
  unsigned long addr = (unsigned long)ptr;
  int shift = M68K_MEM_SHIFT;
  int i;

  if (!is_sub) {
    r8map = m68k_read8_map;
    r16map = m68k_read16_map;
    w8map = m68k_write8_map;
    w16map = m68k_write16_map;
  } else {
    r8map = s68k_read8_map;
    r16map = s68k_read16_map;
    w8map = s68k_write8_map;
    w16map = s68k_write16_map;
  }

  addr -= start_addr;
  addr >>= 1;
  for (i = start_addr >> shift; i <= end_addr >> shift; i++)
    r8map[i] = r16map[i] = w8map[i] = w16map[i] = addr;
}

static u32 m68k_unmapped_read8(u32 a)
{
  elprintf(EL_UIO, "m68k unmapped r8  [%06x] @%06x", a, SekPc);
  return 0; // assume pulldown, as if MegaCD2 was attached
}

static u32 m68k_unmapped_read16(u32 a)
{
  elprintf(EL_UIO, "m68k unmapped r16 [%06x] @%06x", a, SekPc);
  return 0;
}

static void m68k_unmapped_write8(u32 a, u32 d)
{
  elprintf(EL_UIO, "m68k unmapped w8  [%06x]   %02x @%06x", a, d & 0xff, SekPc);
}

static void m68k_unmapped_write16(u32 a, u32 d)
{
  elprintf(EL_UIO, "m68k unmapped w16 [%06x] %04x @%06x", a, d & 0xffff, SekPc);
}

void m68k_map_unmap(int start_addr, int end_addr)
{
  unsigned long addr;
  int shift = M68K_MEM_SHIFT;
  int i;

  addr = (unsigned long)m68k_unmapped_read8;
  for (i = start_addr >> shift; i <= end_addr >> shift; i++)
    m68k_read8_map[i] = (addr >> 1) | (1 << 31);

  addr = (unsigned long)m68k_unmapped_read16;
  for (i = start_addr >> shift; i <= end_addr >> shift; i++)
    m68k_read16_map[i] = (addr >> 1) | (1 << 31);

  addr = (unsigned long)m68k_unmapped_write8;
  for (i = start_addr >> shift; i <= end_addr >> shift; i++)
    m68k_write8_map[i] = (addr >> 1) | (1 << 31);

  addr = (unsigned long)m68k_unmapped_write16;
  for (i = start_addr >> shift; i <= end_addr >> shift; i++)
    m68k_write16_map[i] = (addr >> 1) | (1 << 31);
}

MAKE_68K_READ8(m68k_read8, m68k_read8_map)
MAKE_68K_READ16(m68k_read16, m68k_read16_map)
MAKE_68K_READ32(m68k_read32, m68k_read16_map)
MAKE_68K_WRITE8(m68k_write8, m68k_write8_map)
MAKE_68K_WRITE16(m68k_write16, m68k_write16_map)
MAKE_68K_WRITE32(m68k_write32, m68k_write16_map)

// -----------------------------------------------------------------

static u32 ym2612_read_local_68k(void);
static int ym2612_write_local(u32 a, u32 d, int is_from_z80);
static void z80_mem_setup(void);


#ifdef EMU_CORE_DEBUG
u32 lastread_a, lastread_d[16]={0,}, lastwrite_cyc_d[16]={0,}, lastwrite_mus_d[16]={0,};
int lrp_cyc=0, lrp_mus=0, lwp_cyc=0, lwp_mus=0;
extern unsigned int ppop;
#endif

#ifdef IO_STATS
void log_io(unsigned int addr, int bits, int rw);
#elif defined(_MSC_VER)
#define log_io
#else
#define log_io(...)
#endif

#if defined(EMU_C68K)
static __inline int PicoMemBase(u32 pc)
{
  int membase=0;

  if (pc<Pico.romsize+4)
  {
    membase=(int)Pico.rom; // Program Counter in Rom
  }
  else if ((pc&0xe00000)==0xe00000)
  {
    membase=(int)Pico.ram-(pc&0xff0000); // Program Counter in Ram
  }
  else
  {
    // Error - Program Counter is invalid
    membase=(int)Pico.rom;
  }

  return membase;
}
#endif


PICO_INTERNAL u32 PicoCheckPc(u32 pc)
{
  u32 ret=0;
#if defined(EMU_C68K)
  pc-=PicoCpuCM68k.membase; // Get real pc
//  pc&=0xfffffe;
  pc&=~1;
  if ((pc<<8) == 0)
  {
    elprintf(EL_STATUS|EL_ANOMALY, "%i:%03i: game crash detected @ %06x\n",
      Pico.m.frame_count, Pico.m.scanline, SekPc);
    return (int)Pico.rom + Pico.romsize; // common crash condition, may happen with bad ROMs
  }

  PicoCpuCM68k.membase=PicoMemBase(pc&0x00ffffff);
  PicoCpuCM68k.membase-=pc&0xff000000;

  ret = PicoCpuCM68k.membase+pc;
#endif
  return ret;
}


PICO_INTERNAL void PicoInitPc(u32 pc)
{
  PicoCheckPc(pc);
}

// -----------------------------------------------------------------
// memmap helpers

static int PadRead(int i)
{
  int pad,value,data_reg;
  pad=~PicoPadInt[i]; // Get inverse of pad MXYZ SACB RLDU
  data_reg=Pico.ioports[i+1];

  // orr the bits, which are set as output
  value = data_reg&(Pico.ioports[i+4]|0x80);

  if (PicoOpt & POPT_6BTN_PAD)
  {
    int phase = Pico.m.padTHPhase[i];

    if(phase == 2 && !(data_reg&0x40)) { // TH
      value|=(pad&0xc0)>>2;              // ?0SA 0000
      return value;
    } else if(phase == 3) {
      if(data_reg&0x40)
        value|=(pad&0x30)|((pad>>8)&0xf);  // ?1CB MXYZ
      else
        value|=((pad&0xc0)>>2)|0x0f;       // ?0SA 1111
      return value;
    }
  }

  if(data_reg&0x40) // TH
       value|=(pad&0x3f);              // ?1CB RLDU
  else value|=((pad&0xc0)>>2)|(pad&3); // ?0SA 00DU

  return value; // will mirror later
}

static u32 io_ports_read(u32 a)
{
  u32 d;
  a = (a>>1) & 0xf;
  switch (a) {
    case 0:  d = Pico.m.hardware; break; // Hardware value (Version register)
    case 1:  d = PadRead(0); break;
    case 2:  d = PadRead(1); break;
    default: d = Pico.ioports[a]; break; // IO ports can be used as RAM
  }
  return d;
}

static void io_ports_write(u32 a, u32 d)
{
  a = (a>>1) & 0xf;

  // 6 button gamepad: if TH went from 0 to 1, gamepad changes state
  if (1 <= a && a <= 2 && (PicoOpt & POPT_6BTN_PAD))
  {
    Pico.m.padDelay[a - 1] = 0;
    if (!(Pico.ioports[a] & 0x40) && (d & 0x40))
      Pico.m.padTHPhase[a - 1]++;
  }

  // cartain IO ports can be used as RAM
  Pico.ioports[a] = d;
}

static void ctl_write_z80busreq(u32 d)
{
  d&=1; d^=1;
  elprintf(EL_BUSREQ, "set_zrun: %i->%i [%i] @%06x", Pico.m.z80Run, d, SekCyclesDone(), SekPc);
  if (d ^ Pico.m.z80Run)
  {
    if (d)
    {
      z80_cycle_cnt = cycles_68k_to_z80(SekCyclesDone());
    }
    else
    {
      z80stopCycle = SekCyclesDone();
      if ((PicoOpt&POPT_EN_Z80) && !Pico.m.z80_reset)
        PicoSyncZ80(z80stopCycle);
    }
    Pico.m.z80Run = d;
  }
}

static void ctl_write_z80reset(u32 d)
{
  d&=1; d^=1;
  elprintf(EL_BUSREQ, "set_zreset: %i->%i [%i] @%06x", Pico.m.z80_reset, d, SekCyclesDone(), SekPc);
  if (d ^ Pico.m.z80_reset)
  {
    if (d)
    {
      if ((PicoOpt&POPT_EN_Z80) && Pico.m.z80Run)
        PicoSyncZ80(SekCyclesDone());
      YM2612ResetChip();
      timers_reset();
    }
    else
    {
      z80_cycle_cnt = cycles_68k_to_z80(SekCyclesDone());
      z80_reset();
    }
    Pico.m.z80_reset = d;
  }
}


// for nonstandard reads
// TODO: mv to carthw
static u32 OtherRead16End(u32 a, int realsize)
{
  u32 d=0;

  // 32x test
/*
  if      (a == 0xa130ec) { d = 0x4d41; goto end; } // MA
  else if (a == 0xa130ee) { d = 0x5253; goto end; } // RS
  else if (a == 0xa15100) { d = 0x0080; goto end; }
  else
*/

  // for games with simple protection devices, discovered by Haze
  // some dumb detection is used, but that should be enough to make things work
  if ((a>>22) == 1 && Pico.romsize >= 512*1024) {
    if      (*(int *)(Pico.rom+0x123e4) == 0x00550c39 && *(int *)(Pico.rom+0x123e8) == 0x00000040) { // Super Bubble Bobble (Unl) [!]
      if      (a == 0x400000) { d=0x55<<8; goto end; }
      else if (a == 0x400002) { d=0x0f<<8; goto end; }
    }
    else if (*(int *)(Pico.rom+0x008c4) == 0x66240055 && *(int *)(Pico.rom+0x008c8) == 0x00404df9) { // Smart Mouse (Unl)
      if      (a == 0x400000) { d=0x55<<8; goto end; }
      else if (a == 0x400002) { d=0x0f<<8; goto end; }
      else if (a == 0x400004) { d=0xaa<<8; goto end; }
      else if (a == 0x400006) { d=0xf0<<8; goto end; }
    }
    else if (*(int *)(Pico.rom+0x00404) == 0x00a90600 && *(int *)(Pico.rom+0x00408) == 0x6708b013) { // King of Fighters '98, The (Unl) [!]
      if      (a == 0x480000 || a == 0x4800e0 || a == 0x4824a0 || a == 0x488880) { d=0xaa<<8; goto end; }
      else if (a == 0x4a8820) { d=0x0a<<8; goto end; }
      // there is also a read @ 0x4F8820 which needs 0, but that is returned in default case
    }
    else if (*(int *)(Pico.rom+0x01b24) == 0x004013f9 && *(int *)(Pico.rom+0x01b28) == 0x00ff0000) { // Mahjong Lover (Unl) [!]
      if      (a == 0x400000) { d=0x90<<8; goto end; }
      else if (a == 0x401000) { d=0xd3<<8; goto end; } // this one doesn't seem to be needed, the code does 2 comparisons and only then
                                                       // checks the result, which is of the above one. Left it just in case.
    }
    else if (*(int *)(Pico.rom+0x05254) == 0x0c3962d0 && *(int *)(Pico.rom+0x05258) == 0x00400055) { // Elf Wor (Unl)
      if      (a == 0x400000) { d=0x55<<8; goto end; }
      else if (a == 0x400004) { d=0xc9<<8; goto end; } // this check is done if the above one fails
      else if (a == 0x400002) { d=0x0f<<8; goto end; }
      else if (a == 0x400006) { d=0x18<<8; goto end; } // similar to above
    }
    // our default behaviour is to return whatever was last written a 0x400000-0x7fffff range (used by Squirrel King (R) [!])
    // Lion King II, The (Unl) [!]  writes @ 400000 and wants to get that val @ 400002 and wites another val
    // @ 400004 which is expected @ 400006, so we really remember 2 values here
    d = Pico.m.prot_bytes[(a>>2)&1]<<8;
  }
  else if (a == 0xa13000 && Pico.romsize >= 1024*1024) {
    if      (*(int *)(Pico.rom+0xc8af0) == 0x30133013 && *(int *)(Pico.rom+0xc8af4) == 0x000f0240) { // Rockman X3 (Unl) [!]
      d=0x0c; goto end;
    }
    else if (*(int *)(Pico.rom+0x28888) == 0x07fc0000 && *(int *)(Pico.rom+0x2888c) == 0x4eb94e75) { // Bug's Life, A (Unl) [!]
      d=0x28; goto end; // does the check from RAM
    }
    else if (*(int *)(Pico.rom+0xc8778) == 0x30133013 && *(int *)(Pico.rom+0xc877c) == 0x000f0240) { // Super Mario Bros. (Unl) [!]
      d=0x0c; goto end; // seems to be the same code as in Rockman X3 (Unl) [!]
    }
    else if (*(int *)(Pico.rom+0xf20ec) == 0x30143013 && *(int *)(Pico.rom+0xf20f0) == 0x000f0200) { // Super Mario 2 1998 (Unl) [!]
      d=0x0a; goto end;
    }
  }
  else if (a == 0xa13002) { // Pocket Monsters (Unl)
    d=0x01; goto end;
  }
  else if (a == 0xa1303E) { // Pocket Monsters (Unl)
    d=0x1f; goto end;
  }
  else if (a == 0x30fe02) {
    // Virtua Racing - just for fun
    // this seems to be some flag that SVP is ready or something similar
    d=1; goto end;
  }

end:
  elprintf(EL_UIO, "strange r%i: [%06x] %04x @%06x", realsize, a&0xffffff, d, SekPc);
  return d;
}

static void OtherWrite8End(u32 a,u32 d,int realsize)
{
#ifdef _ASM_MEMORY_C
  // special ROM hardware (currently only banking and sram reg supported)
  if((a&0xfffff1) == 0xA130F1) {
    PicoWriteRomHW_SSF2(a, d); // SSF2 or SRAM
    return;
  }
#else
  // sram access register
  if(a == 0xA130F1) {
    elprintf(EL_SRAMIO, "sram reg=%02x", d);
    Pico.m.sram_status &= ~(SRS_MAPPED|SRS_READONLY);
    Pico.m.sram_status |= (u8)(d&3);
    return;
  }
#endif
  elprintf(EL_UIO, "strange w%i: %06x, %08x @%06x", realsize, a&0xffffff, d, SekPc);

  // for games with simple protection devices, discovered by Haze
  if ((a>>22) == 1)
    Pico.m.prot_bytes[(a>>2)&1] = (u8)d;
}

// -----------------------------------------------------------------

// cart (save) RAM area (usually 0x200000 - ...)
static u32 PicoRead8_sram(u32 a)
{
  int srs = Pico.m.sram_status;
  u32 d;
  if (SRam.end >= a && a >= SRam.start && (srs & (SRS_MAPPED|SRS_EEPROM)))
  {
    if (srs & SRS_EEPROM)
      d = EEPROM_read();
    else
      d = *(u8 *)(SRam.data - SRam.start + a);
    elprintf(EL_SRAMIO, "sram r8 [%06x] %02x @ %06x", a, d, SekPc);
    return d;
  }

  if (a < Pico.romsize)
    return Pico.rom[a ^ 1];
  
  return m68k_unmapped_read8(a);
}

static u32 PicoRead16_sram(u32 a)
{
  int srs = Pico.m.sram_status;
  u32 d;
  if (SRam.end >= a && a >= SRam.start && (srs & (SRS_MAPPED|SRS_EEPROM)))
  {
    if (srs & SRS_EEPROM) {
      d = EEPROM_read();
      d |= d << 8;
    } else {
      u8 *pm = (u8 *)(SRam.data - SRam.start + a);
      d  = pm[0] << 8;
      d |= pm[1];
    }
    elprintf(EL_SRAMIO, "sram r16 [%06x] %04x @ %06x", a, d, SekPc);
    return d;
  }

  if (a < Pico.romsize)
    return *(u16 *)(Pico.rom + a);

  return m68k_unmapped_read16(a);
}

static void PicoWrite8_sram(u32 a, u32 d)
{
  unsigned int srs = Pico.m.sram_status;
  elprintf(EL_SRAMIO, "sram wX  [%06x] %02x @ %06x", a, d & 0xffff, SekPc);
  if (srs & SRS_EEPROM) // EEPROM write
  {
    // this diff must be at most 16 for NBA Jam to work
    if (SekCyclesDoneT() - lastSSRamWrite < 16) {
      // just update pending state
      elprintf(EL_EEPROM, "eeprom: skip because cycles=%i",
          SekCyclesDoneT() - lastSSRamWrite);
      EEPROM_upd_pending(a, d);
    } else {
      EEPROM_write(srs >> 6); // execute pending
      EEPROM_upd_pending(a, d);
      if ((srs ^ Pico.m.sram_status) & 0xc0) // update time only if SDA/SCL changed
        lastSSRamWrite = SekCyclesDoneT();
    }
  }
  else if (!(srs & SRS_READONLY)) {
    u8 *pm=(u8 *)(SRam.data - SRam.start + a);
    if (*pm != (u8)d) {
      SRam.changed = 1;
      *pm = (u8)d;
    }
  }
}

static void PicoWrite16_sram(u32 a, u32 d)
{
  // XXX: hardware could easily use MSB too..
  PicoWrite8_sram(a + 1, d);
}

// z80 area (0xa00000 - 0xa0ffff)
// TODO: verify mirrors VDP and bank reg (bank area mirroring verified)
static u32 PicoRead8_z80(u32 a)
{
  u32 d = 0xff;
  if ((Pico.m.z80Run & 1) || Pico.m.z80_reset) {
    elprintf(EL_ANOMALY, "68k z80 read with no bus! [%06x] @ %06x", a, SekPc);
    // open bus. Pulled down if MegaCD2 is attached.
    return 0;
  }

  if ((a & 0x4000) == 0x0000)
    d = Pico.zram[a & 0x1fff];
  else if ((a & 0x6000) == 0x4000) // 0x4000-0x5fff
    d = ym2612_read_local_68k(); 
  else
    elprintf(EL_UIO|EL_ANOMALY, "68k bad read [%06x] @%06x", a, SekPc);
  return d;
}

static u32 PicoRead16_z80(u32 a)
{
  u32 d = PicoRead8_z80(a);
  return d | (d << 8);
}

static void PicoWrite8_z80(u32 a, u32 d)
{
  if ((Pico.m.z80Run & 1) || Pico.m.z80_reset) {
    // verified on real hw
    elprintf(EL_ANOMALY, "68k z80 write with no bus or reset! [%06x] %02x @ %06x", a, d&0xff, SekPc);
    return;
  }

  if ((a & 0x4000) == 0x0000) { // z80 RAM
    SekCyclesBurn(2); // hack
    Pico.zram[a & 0x1fff] = (u8)d;
    return;
  }
  if ((a & 0x6000) == 0x4000) { // FM Sound
    if (PicoOpt & POPT_EN_FM)
      emustatus |= ym2612_write_local(a&3, d&0xff, 0)&1;
    return;
  }
  // TODO: probably other VDP access too? Maybe more mirrors?
  if ((a & 0x7ff9) == 0x7f11) { // PSG Sound
    if (PicoOpt & POPT_EN_PSG)
      SN76496Write(d);
    return;
  }
#if !defined(_ASM_MEMORY_C) || defined(_ASM_MEMORY_C_AMIPS)
  if ((a & 0x7f00) == 0x6000) // Z80 BANK register
  {
    Pico.m.z80_bank68k >>= 1;
    Pico.m.z80_bank68k |= d << 8;
    Pico.m.z80_bank68k &= 0x1ff; // 9 bits and filled in the new top one
    elprintf(EL_Z80BNK, "z80 bank=%06x", Pico.m.z80_bank68k << 15);
    return;
  }
#endif
  elprintf(EL_UIO|EL_ANOMALY, "68k bad write [%06x] %02x @ %06x", a, d&0xff, SekPc);
}

static void PicoWrite16_z80(u32 a, u32 d)
{
  // for RAM, only most significant byte is sent
  // TODO: verify remaining accesses
  PicoWrite8_z80(a, d >> 8);
}

// IO/control area (0xa10000 - 0xa1ffff)
u32 PicoRead8_io(u32 a)
{
  u32 d;

  if ((a & 0xffe0) == 0x0000) { // I/O ports
    d = io_ports_read(a);
    goto end;
  }

  // faking open bus (MegaCD pulldowns don't work here curiously)
  d = Pico.m.rotate++;
  d ^= d << 6;

  // bit8 seems to be readable in this range
  if ((a & 0xfc01) == 0x1000)
    d &= ~0x01;

  if ((a & 0xff01) == 0x1100) { // z80 busreq (verified)
    d |= (Pico.m.z80Run | Pico.m.z80_reset) & 1;
    elprintf(EL_BUSREQ, "get_zrun: %02x [%i] @%06x", d, SekCyclesDone(), SekPc);
    goto end;
  }

  d = m68k_unmapped_read8(a);
end:
  return d;
}

u32 PicoRead16_io(u32 a)
{
  u32 d;

  if ((a & 0xffe0) == 0x0000) { // I/O ports
    d = io_ports_read(a);
    goto end;
  }

  // faking open bus
  d = (Pico.m.rotate += 0x41);
  d ^= (d << 5) ^ (d << 8);

  // bit8 seems to be readable in this range
  if ((a & 0xfc00) == 0x1000)
    d &= ~0x0100;

  if ((a & 0xff00) == 0x1100) { // z80 busreq
    d |= ((Pico.m.z80Run | Pico.m.z80_reset) & 1) << 8;
    elprintf(EL_BUSREQ, "get_zrun: %04x [%i] @%06x", d, SekCyclesDone(), SekPc);
    goto end;
  }

  d = m68k_unmapped_read16(a);
end:
  return d;
}

void PicoWrite8_io(u32 a, u32 d)
{
  if ((a & 0xffe1) == 0x0001) { // I/O ports (verified: only LSB!)
    io_ports_write(a, d);
    return;
  }
  if ((a & 0xff01) == 0x1100) { // z80 busreq
    ctl_write_z80busreq(d);
    return;
  }
  if ((a & 0xff01) == 0x1200) { // z80 reset
    ctl_write_z80reset(d);
    return;
  }
  if (a == 0xa130f1) { // sram access register
    elprintf(EL_SRAMIO, "sram reg=%02x", d);
    Pico.m.sram_status &= ~(SRS_MAPPED|SRS_READONLY);
    Pico.m.sram_status |= (u8)(d & 3);
    return;
  }
  m68k_unmapped_write8(a, d);
}

void PicoWrite16_io(u32 a, u32 d)
{
  if ((a & 0xffe0) == 0x0000) { // I/O ports (verified: only LSB!)
    io_ports_write(a, d);
    return;
  }
  if ((a & 0xff00) == 0x1100) { // z80 busreq
    ctl_write_z80busreq(d >> 8);
    return;
  }
  if ((a & 0xff00) == 0x1200) { // z80 reset
    ctl_write_z80reset(d >> 8);
    return;
  }
  if (a == 0xa130f0) { // sram access register
    elprintf(EL_SRAMIO, "sram reg=%02x", d);
    Pico.m.sram_status &= ~(SRS_MAPPED|SRS_READONLY);
    Pico.m.sram_status |= (u8)(d & 3);
    return;
  }
  m68k_unmapped_write16(a, d);
}

// VDP area (0xc00000 - 0xdfffff)
// TODO: verify if lower byte goes to PSG on word writes
static u32 PicoRead8_vdp(u32 a)
{
  if ((a & 0x00e0) == 0x0000)
    return PicoVideoRead8(a);

  elprintf(EL_UIO|EL_ANOMALY, "68k bad read [%06x] @%06x", a, SekPc);
  return 0;
}

static u32 PicoRead16_vdp(u32 a)
{
  if ((a & 0x00e0) == 0x0000)
    return PicoVideoRead(a);

  elprintf(EL_UIO|EL_ANOMALY, "68k bad read [%06x] @%06x", a, SekPc);
  return 0;
}

static void PicoWrite8_vdp(u32 a, u32 d)
{
  if ((a & 0x00f9) == 0x0011) { // PSG Sound
    if (PicoOpt & POPT_EN_PSG)
      SN76496Write(d);
    return;
  }
  if ((a & 0x00e0) == 0x0000) {
    d &= 0xff;
    PicoVideoWrite(a, d | (d << 8));
    return;
  }

  elprintf(EL_UIO|EL_ANOMALY, "68k bad write [%06x] %02x @%06x", a, d & 0xff, SekPc);
}

static void PicoWrite16_vdp(u32 a, u32 d)
{
  if ((a & 0x00f9) == 0x0010) { // PSG Sound
    if (PicoOpt & POPT_EN_PSG)
      SN76496Write(d);
    return;
  }
  if ((a & 0x00e0) == 0x0000) {
    PicoVideoWrite(a, d);
    return;
  }

  elprintf(EL_UIO|EL_ANOMALY, "68k bad write [%06x] %04x @%06x", a, d & 0xffff, SekPc);
}

// -----------------------------------------------------------------

// TODO: rm
static void OtherWrite16End(u32 a,u32 d,int realsize)
{
  PicoWrite8Hook(a,  d>>8, realsize);
  PicoWrite8Hook(a+1,d&0xff, realsize);
}

u32  (*PicoRead16Hook) (u32 a, int realsize) = OtherRead16End;
void (*PicoWrite8Hook) (u32 a, u32 d, int realsize) = OtherWrite8End;
void (*PicoWrite16Hook)(u32 a, u32 d, int realsize) = OtherWrite16End;

PICO_INTERNAL void PicoMemResetHooks(void)
{
  // default unmapped/cart specific handlers
  PicoRead16Hook = OtherRead16End;
  PicoWrite8Hook = OtherWrite8End;
  PicoWrite16Hook = OtherWrite16End;
}

#ifdef EMU_M68K
static void m68k_mem_setup(void);
#endif

PICO_INTERNAL void PicoMemSetup(void)
{
  int mask, rs, a;

  // setup the memory map
  cpu68k_map_set(m68k_read8_map,   0x000000, 0xffffff, m68k_unmapped_read8, 1);
  cpu68k_map_set(m68k_read16_map,  0x000000, 0xffffff, m68k_unmapped_read16, 1);
  cpu68k_map_set(m68k_write8_map,  0x000000, 0xffffff, m68k_unmapped_write8, 1);
  cpu68k_map_set(m68k_write16_map, 0x000000, 0xffffff, m68k_unmapped_write16, 1);

  // ROM
  // align to bank size. We know ROM loader allocated enough for this
  mask = (1 << M68K_MEM_SHIFT) - 1;
  rs = (Pico.romsize + mask) & ~mask;
  cpu68k_map_set(m68k_read8_map,  0x000000, rs - 1, Pico.rom, 0);
  cpu68k_map_set(m68k_read16_map, 0x000000, rs - 1, Pico.rom, 0);

  // Common case of on-cart (save) RAM, usually at 0x200000-...
  rs = SRam.end - SRam.start;
  if (rs > 0 && SRam.data != NULL) {
    rs = (rs + mask) & ~mask;
    if (SRam.start + rs >= 0x1000000)
      rs = 0x1000000 - SRam.start;
    cpu68k_map_set(m68k_read8_map,   SRam.start, SRam.start + rs - 1, PicoRead8_sram, 1);
    cpu68k_map_set(m68k_read16_map,  SRam.start, SRam.start + rs - 1, PicoRead16_sram, 1);
    cpu68k_map_set(m68k_write8_map,  SRam.start, SRam.start + rs - 1, PicoWrite8_sram, 1);
    cpu68k_map_set(m68k_write16_map, SRam.start, SRam.start + rs - 1, PicoWrite16_sram, 1);
  }

  // Z80 region
  cpu68k_map_set(m68k_read8_map,   0xa00000, 0xa0ffff, PicoRead8_z80, 1);
  cpu68k_map_set(m68k_read16_map,  0xa00000, 0xa0ffff, PicoRead16_z80, 1);
  cpu68k_map_set(m68k_write8_map,  0xa00000, 0xa0ffff, PicoWrite8_z80, 1);
  cpu68k_map_set(m68k_write16_map, 0xa00000, 0xa0ffff, PicoWrite16_z80, 1);

  // IO/control region
  cpu68k_map_set(m68k_read8_map,   0xa10000, 0xa1ffff, PicoRead8_io, 1);
  cpu68k_map_set(m68k_read16_map,  0xa10000, 0xa1ffff, PicoRead16_io, 1);
  cpu68k_map_set(m68k_write8_map,  0xa10000, 0xa1ffff, PicoWrite8_io, 1);
  cpu68k_map_set(m68k_write16_map, 0xa10000, 0xa1ffff, PicoWrite16_io, 1);

  // VDP region
  for (a = 0xc00000; a < 0xe00000; a += 0x010000) {
    if ((a & 0xe700e0) != 0xc00000)
      continue;
    cpu68k_map_set(m68k_read8_map,   a, a + 0xffff, PicoRead8_vdp, 1);
    cpu68k_map_set(m68k_read16_map,  a, a + 0xffff, PicoRead16_vdp, 1);
    cpu68k_map_set(m68k_write8_map,  a, a + 0xffff, PicoWrite8_vdp, 1);
    cpu68k_map_set(m68k_write16_map, a, a + 0xffff, PicoWrite16_vdp, 1);
  }

  // RAM and it's mirrors
  for (a = 0xe00000; a < 0x1000000; a += 0x010000) {
    cpu68k_map_set(m68k_read8_map,   a, a + 0xffff, Pico.ram, 0);
    cpu68k_map_set(m68k_read16_map,  a, a + 0xffff, Pico.ram, 0);
    cpu68k_map_set(m68k_write8_map,  a, a + 0xffff, Pico.ram, 0);
    cpu68k_map_set(m68k_write16_map, a, a + 0xffff, Pico.ram, 0);
  }

  // Setup memory callbacks:
#ifdef EMU_C68K
  PicoCpuCM68k.checkpc = PicoCheckPc;
  PicoCpuCM68k.fetch8  = PicoCpuCM68k.read8  = m68k_read8;
  PicoCpuCM68k.fetch16 = PicoCpuCM68k.read16 = m68k_read16;
  PicoCpuCM68k.fetch32 = PicoCpuCM68k.read32 = m68k_read32;
  PicoCpuCM68k.write8  = m68k_write8;
  PicoCpuCM68k.write16 = m68k_write16;
  PicoCpuCM68k.write32 = m68k_write32;
#endif
#ifdef EMU_F68K
  PicoCpuFM68k.read_byte  = m68k_read8;
  PicoCpuFM68k.read_word  = m68k_read16;
  PicoCpuFM68k.read_long  = m68k_read32;
  PicoCpuFM68k.write_byte = m68k_write8;
  PicoCpuFM68k.write_word = m68k_write16;
  PicoCpuFM68k.write_long = m68k_write32;

  // setup FAME fetchmap
  {
    int i;
    // by default, point everything to first 64k of ROM
    for (i = 0; i < M68K_FETCHBANK1; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.rom - (i<<(24-FAMEC_FETCHBITS));
    // now real ROM
    for (i = 0; i < M68K_FETCHBANK1 && (i<<(24-FAMEC_FETCHBITS)) < Pico.romsize; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.rom;
    // .. and RAM
    for (i = M68K_FETCHBANK1*14/16; i < M68K_FETCHBANK1; i++)
      PicoCpuFM68k.Fetch[i] = (unsigned int)Pico.ram - (i<<(24-FAMEC_FETCHBITS));
  }
#endif
#ifdef EMU_M68K
  m68k_mem_setup();
#endif

  z80_mem_setup();
}

/* some nasty things below :( */
#ifdef EMU_M68K
unsigned int (*pm68k_read_memory_8) (unsigned int address) = NULL;
unsigned int (*pm68k_read_memory_16)(unsigned int address) = NULL;
unsigned int (*pm68k_read_memory_32)(unsigned int address) = NULL;
void (*pm68k_write_memory_8) (unsigned int address, unsigned char  value) = NULL;
void (*pm68k_write_memory_16)(unsigned int address, unsigned short value) = NULL;
void (*pm68k_write_memory_32)(unsigned int address, unsigned int   value) = NULL;
unsigned int (*pm68k_read_memory_pcr_8) (unsigned int address) = NULL;
unsigned int (*pm68k_read_memory_pcr_16)(unsigned int address) = NULL;
unsigned int (*pm68k_read_memory_pcr_32)(unsigned int address) = NULL;

// these are here for core debugging mode
static unsigned int  m68k_read_8 (unsigned int a, int do_fake)
{
  a&=0xffffff;
  if(a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k)    return *(u8 *)(Pico.rom+(a^1)); // Rom
#ifdef EMU_CORE_DEBUG
  if(do_fake&&((ppop&0x3f)==0x3a||(ppop&0x3f)==0x3b)) return lastread_d[lrp_mus++&15];
#endif
  return pm68k_read_memory_pcr_8(a);
}
static unsigned int  m68k_read_16(unsigned int a, int do_fake)
{
  a&=0xffffff;
  if(a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k)    return *(u16 *)(Pico.rom+(a&~1)); // Rom
#ifdef EMU_CORE_DEBUG
  if(do_fake&&((ppop&0x3f)==0x3a||(ppop&0x3f)==0x3b)) return lastread_d[lrp_mus++&15];
#endif
  return pm68k_read_memory_pcr_16(a);
}
static unsigned int  m68k_read_32(unsigned int a, int do_fake)
{
  a&=0xffffff;
  if(a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k) { u16 *pm=(u16 *)(Pico.rom+(a&~1)); return (pm[0]<<16)|pm[1]; }
#ifdef EMU_CORE_DEBUG
  if(do_fake&&((ppop&0x3f)==0x3a||(ppop&0x3f)==0x3b)) return lastread_d[lrp_mus++&15];
#endif
  return pm68k_read_memory_pcr_32(a);
}

unsigned int m68k_read_pcrelative_8 (unsigned int a)   { return m68k_read_8 (a, 1); }
unsigned int m68k_read_pcrelative_16(unsigned int a)   { return m68k_read_16(a, 1); }
unsigned int m68k_read_pcrelative_32(unsigned int a)   { return m68k_read_32(a, 1); }
unsigned int m68k_read_immediate_16(unsigned int a)    { return m68k_read_16(a, 0); }
unsigned int m68k_read_immediate_32(unsigned int a)    { return m68k_read_32(a, 0); }
unsigned int m68k_read_disassembler_8 (unsigned int a) { return m68k_read_8 (a, 0); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_16(a, 0); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_32(a, 0); }

static unsigned int m68k_read_memory_pcr_8(unsigned int a)
{
  if((a&0xe00000)==0xe00000) return *(u8 *)(Pico.ram+((a^1)&0xffff)); // Ram
  return 0;
}

static unsigned int m68k_read_memory_pcr_16(unsigned int a)
{
  if((a&0xe00000)==0xe00000) return *(u16 *)(Pico.ram+(a&0xfffe)); // Ram
  return 0;
}

static unsigned int m68k_read_memory_pcr_32(unsigned int a)
{
  if((a&0xe00000)==0xe00000) { u16 *pm=(u16 *)(Pico.ram+(a&0xfffe)); return (pm[0]<<16)|pm[1]; } // Ram
  return 0;
}

#ifdef EMU_CORE_DEBUG
// ROM only
unsigned int m68k_read_memory_8(unsigned int a)
{
  u8 d;
  if (a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k)
       d = *(u8 *) (Pico.rom+(a^1));
  else d = (u8) lastread_d[lrp_mus++&15];
  elprintf(EL_IO, "r8_mu : %06x,   %02x @%06x", a&0xffffff, d, SekPc);
  return d;
}
unsigned int m68k_read_memory_16(unsigned int a)
{
  u16 d;
  if (a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k)
       d = *(u16 *)(Pico.rom+(a&~1));
  else d = (u16) lastread_d[lrp_mus++&15];
  elprintf(EL_IO, "r16_mu: %06x, %04x @%06x", a&0xffffff, d, SekPc);
  return d;
}
unsigned int m68k_read_memory_32(unsigned int a)
{
  u32 d;
  if (a<Pico.romsize && m68ki_cpu_p==&PicoCpuMM68k)
       { u16 *pm=(u16 *)(Pico.rom+(a&~1));d=(pm[0]<<16)|pm[1]; }
  else if (a <= 0x78) d = m68k_read_32(a, 0);
  else d = lastread_d[lrp_mus++&15];
  elprintf(EL_IO, "r32_mu: %06x, %08x @%06x", a&0xffffff, d, SekPc);
  return d;
}

// ignore writes, Cyclone already done that
void m68k_write_memory_8(unsigned int address, unsigned int value)  { lastwrite_mus_d[lwp_mus++&15] = value; }
void m68k_write_memory_16(unsigned int address, unsigned int value) { lastwrite_mus_d[lwp_mus++&15] = value; }
void m68k_write_memory_32(unsigned int address, unsigned int value) { lastwrite_mus_d[lwp_mus++&15] = value; }

#else // if !EMU_CORE_DEBUG

/* it appears that Musashi doesn't always mask the unused bits */
unsigned int m68k_read_memory_8 (unsigned int address) { return pm68k_read_memory_8 (address) & 0xff; }
unsigned int m68k_read_memory_16(unsigned int address) { return pm68k_read_memory_16(address) & 0xffff; }
unsigned int m68k_read_memory_32(unsigned int address) { return pm68k_read_memory_32(address); }
void m68k_write_memory_8 (unsigned int address, unsigned int value) { pm68k_write_memory_8 (address, (u8)value); }
void m68k_write_memory_16(unsigned int address, unsigned int value) { pm68k_write_memory_16(address,(u16)value); }
void m68k_write_memory_32(unsigned int address, unsigned int value) { pm68k_write_memory_32(address, value); }
#endif // !EMU_CORE_DEBUG

static void m68k_mem_setup(void)
{
  pm68k_read_memory_8  = m68k_read8;
  pm68k_read_memory_16 = m68k_read16;
  pm68k_read_memory_32 = m68k_read32;
  pm68k_write_memory_8  = m68k_write8;
  pm68k_write_memory_16 = m68k_write16;
  pm68k_write_memory_32 = m68k_write32;
  pm68k_read_memory_pcr_8  = m68k_read_memory_pcr_8;
  pm68k_read_memory_pcr_16 = m68k_read_memory_pcr_16;
  pm68k_read_memory_pcr_32 = m68k_read_memory_pcr_32;
}
#endif // EMU_M68K


// -----------------------------------------------------------------

static int get_scanline(int is_from_z80)
{
  if (is_from_z80) {
    int cycles = z80_cyclesDone();
    while (cycles - z80_scanline_cycles >= 228)
      z80_scanline++, z80_scanline_cycles += 228;
    return z80_scanline;
  }

  return Pico.m.scanline;
}

/* probably should not be in this file, but it's near related code here */
void ym2612_sync_timers(int z80_cycles, int mode_old, int mode_new)
{
  int xcycles = z80_cycles << 8;

  /* check for overflows */
  if ((mode_old & 4) && xcycles > timer_a_next_oflow)
    ym2612.OPN.ST.status |= 1;

  if ((mode_old & 8) && xcycles > timer_b_next_oflow)
    ym2612.OPN.ST.status |= 2;

  /* update timer a */
  if (mode_old & 1)
    while (xcycles > timer_a_next_oflow)
      timer_a_next_oflow += timer_a_step;

  if ((mode_old ^ mode_new) & 1) // turning on/off
  {
    if (mode_old & 1)
      timer_a_next_oflow = TIMER_NO_OFLOW;
    else
      timer_a_next_oflow = xcycles + timer_a_step;
  }
  if (mode_new & 1)
    elprintf(EL_YMTIMER, "timer a upd to %i @ %i", timer_a_next_oflow>>8, z80_cycles);

  /* update timer b */
  if (mode_old & 2)
    while (xcycles > timer_b_next_oflow)
      timer_b_next_oflow += timer_b_step;

  if ((mode_old ^ mode_new) & 2)
  {
    if (mode_old & 2)
      timer_b_next_oflow = TIMER_NO_OFLOW;
    else
      timer_b_next_oflow = xcycles + timer_b_step;
  }
  if (mode_new & 2)
    elprintf(EL_YMTIMER, "timer b upd to %i @ %i", timer_b_next_oflow>>8, z80_cycles);
}

// ym2612 DAC and timer I/O handlers for z80
static int ym2612_write_local(u32 a, u32 d, int is_from_z80)
{
  int addr;

  a &= 3;
  if (a == 1 && ym2612.OPN.ST.address == 0x2a) /* DAC data */
  {
    int scanline = get_scanline(is_from_z80);
    //elprintf(EL_STATUS, "%03i -> %03i dac w %08x z80 %i", PsndDacLine, scanline, d, is_from_z80);
    ym2612.dacout = ((int)d - 0x80) << 6;
    if (PsndOut && ym2612.dacen && scanline >= PsndDacLine)
      PsndDoDAC(scanline);
    return 0;
  }

  switch (a)
  {
    case 0: /* address port 0 */
      ym2612.OPN.ST.address = d;
      ym2612.addr_A1 = 0;
#ifdef __GP2X__
      if (PicoOpt & POPT_EXT_FM) YM2612Write_940(a, d, -1);
#endif
      return 0;

    case 1: /* data port 0    */
      if (ym2612.addr_A1 != 0)
        return 0;

      addr = ym2612.OPN.ST.address;
      ym2612.REGS[addr] = d;

      switch (addr)
      {
        case 0x24: // timer A High 8
        case 0x25: { // timer A Low 2
          int TAnew = (addr == 0x24) ? ((ym2612.OPN.ST.TA & 0x03)|(((int)d)<<2))
                                     : ((ym2612.OPN.ST.TA & 0x3fc)|(d&3));
          if (ym2612.OPN.ST.TA != TAnew)
          {
            //elprintf(EL_STATUS, "timer a set %i", TAnew);
            ym2612.OPN.ST.TA = TAnew;
            //ym2612.OPN.ST.TAC = (1024-TAnew)*18;
            //ym2612.OPN.ST.TAT = 0;
            timer_a_step = TIMER_A_TICK_ZCYCLES * (1024 - TAnew);
            if (ym2612.OPN.ST.mode & 1) {
              // this is not right, should really be done on overflow only
              int cycles = is_from_z80 ? z80_cyclesDone() : cycles_68k_to_z80(SekCyclesDone());
              timer_a_next_oflow = (cycles << 8) + timer_a_step;
            }
            elprintf(EL_YMTIMER, "timer a set to %i, %i", 1024 - TAnew, timer_a_next_oflow>>8);
          }
          return 0;
        }
        case 0x26: // timer B
          if (ym2612.OPN.ST.TB != d) {
            //elprintf(EL_STATUS, "timer b set %i", d);
            ym2612.OPN.ST.TB = d;
            //ym2612.OPN.ST.TBC = (256-d) * 288;
            //ym2612.OPN.ST.TBT  = 0;
            timer_b_step = TIMER_B_TICK_ZCYCLES * (256 - d); // 262800
            if (ym2612.OPN.ST.mode & 2) {
              int cycles = is_from_z80 ? z80_cyclesDone() : cycles_68k_to_z80(SekCyclesDone());
              timer_b_next_oflow = (cycles << 8) + timer_b_step;
            }
            elprintf(EL_YMTIMER, "timer b set to %i, %i", 256 - d, timer_b_next_oflow>>8);
          }
          return 0;
        case 0x27: { /* mode, timer control */
          int old_mode = ym2612.OPN.ST.mode;
          int cycles = is_from_z80 ? z80_cyclesDone() : cycles_68k_to_z80(SekCyclesDone());
          ym2612.OPN.ST.mode = d;

          elprintf(EL_YMTIMER, "st mode %02x", d);
          ym2612_sync_timers(cycles, old_mode, d);

          /* reset Timer a flag */
          if (d & 0x10)
            ym2612.OPN.ST.status &= ~1;

          /* reset Timer b flag */
          if (d & 0x20)
            ym2612.OPN.ST.status &= ~2;

          if ((d ^ old_mode) & 0xc0) {
#ifdef __GP2X__
            if (PicoOpt & POPT_EXT_FM) return YM2612Write_940(a, d, get_scanline(is_from_z80));
#endif
            return 1;
          }
          return 0;
        }
        case 0x2b: { /* DAC Sel  (YM2612) */
          int scanline = get_scanline(is_from_z80);
          ym2612.dacen = d & 0x80;
          if (d & 0x80) PsndDacLine = scanline;
#ifdef __GP2X__
          if (PicoOpt & POPT_EXT_FM) YM2612Write_940(a, d, scanline);
#endif
          return 0;
        }
      }
      break;

    case 2: /* address port 1 */
      ym2612.OPN.ST.address = d;
      ym2612.addr_A1 = 1;
#ifdef __GP2X__
      if (PicoOpt & POPT_EXT_FM) YM2612Write_940(a, d, -1);
#endif
      return 0;

    case 3: /* data port 1    */
      if (ym2612.addr_A1 != 1)
        return 0;

      addr = ym2612.OPN.ST.address | 0x100;
      ym2612.REGS[addr] = d;
      break;
  }

#ifdef __GP2X__
  if (PicoOpt & POPT_EXT_FM)
    return YM2612Write_940(a, d, get_scanline(is_from_z80));
#endif
  return YM2612Write_(a, d);
}


#define ym2612_read_local() \
  if (xcycles >= timer_a_next_oflow) \
    ym2612.OPN.ST.status |= (ym2612.OPN.ST.mode >> 2) & 1; \
  if (xcycles >= timer_b_next_oflow) \
    ym2612.OPN.ST.status |= (ym2612.OPN.ST.mode >> 2) & 2

static u32 MEMH_FUNC ym2612_read_local_z80(void)
{
  int xcycles = z80_cyclesDone() << 8;

  ym2612_read_local();

  elprintf(EL_YMTIMER, "timer z80 read %i, sched %i, %i @ %i|%i", ym2612.OPN.ST.status,
      timer_a_next_oflow>>8, timer_b_next_oflow>>8, xcycles >> 8, (xcycles >> 8) / 228);
  return ym2612.OPN.ST.status;
}

static u32 ym2612_read_local_68k(void)
{
  int xcycles = cycles_68k_to_z80(SekCyclesDone()) << 8;

  ym2612_read_local();

  elprintf(EL_YMTIMER, "timer 68k read %i, sched %i, %i @ %i|%i", ym2612.OPN.ST.status,
      timer_a_next_oflow>>8, timer_b_next_oflow>>8, xcycles >> 8, (xcycles >> 8) / 228);
  return ym2612.OPN.ST.status;
}

void ym2612_pack_state(void)
{
  // timers are saved as tick counts, in 16.16 int format
  int tac, tat = 0, tbc, tbt = 0;
  tac = 1024 - ym2612.OPN.ST.TA;
  tbc = 256  - ym2612.OPN.ST.TB;
  if (timer_a_next_oflow != TIMER_NO_OFLOW)
    tat = (int)((double)(timer_a_step - timer_a_next_oflow) / (double)timer_a_step * tac * 65536);
  if (timer_b_next_oflow != TIMER_NO_OFLOW)
    tbt = (int)((double)(timer_b_step - timer_b_next_oflow) / (double)timer_b_step * tbc * 65536);
  elprintf(EL_YMTIMER, "save: timer a %i/%i", tat >> 16, tac);
  elprintf(EL_YMTIMER, "save: timer b %i/%i", tbt >> 16, tbc);

#ifdef __GP2X__
  if (PicoOpt & POPT_EXT_FM)
    YM2612PicoStateSave2_940(tat, tbt);
  else
#endif
    YM2612PicoStateSave2(tat, tbt);
}

void ym2612_unpack_state(void)
{
  int i, ret, tac, tat, tbc, tbt;
  YM2612PicoStateLoad();

  // feed all the registers and update internal state
  for (i = 0x20; i < 0xA0; i++) {
    ym2612_write_local(0, i, 0);
    ym2612_write_local(1, ym2612.REGS[i], 0);
  }
  for (i = 0x30; i < 0xA0; i++) {
    ym2612_write_local(2, i, 0);
    ym2612_write_local(3, ym2612.REGS[i|0x100], 0);
  }
  for (i = 0xAF; i >= 0xA0; i--) { // must apply backwards
    ym2612_write_local(2, i, 0);
    ym2612_write_local(3, ym2612.REGS[i|0x100], 0);
    ym2612_write_local(0, i, 0);
    ym2612_write_local(1, ym2612.REGS[i], 0);
  }
  for (i = 0xB0; i < 0xB8; i++) {
    ym2612_write_local(0, i, 0);
    ym2612_write_local(1, ym2612.REGS[i], 0);
    ym2612_write_local(2, i, 0);
    ym2612_write_local(3, ym2612.REGS[i|0x100], 0);
  }

#ifdef __GP2X__
  if (PicoOpt & POPT_EXT_FM)
    ret = YM2612PicoStateLoad2_940(&tat, &tbt);
  else
#endif
    ret = YM2612PicoStateLoad2(&tat, &tbt);
  if (ret != 0) {
    elprintf(EL_STATUS, "old ym2612 state");
    return; // no saved timers
  }

  tac = (1024 - ym2612.OPN.ST.TA) << 16;
  tbc = (256  - ym2612.OPN.ST.TB) << 16;
  if (ym2612.OPN.ST.mode & 1)
    timer_a_next_oflow = (int)((double)(tac - tat) / (double)tac * timer_a_step);
  else
    timer_a_next_oflow = TIMER_NO_OFLOW;
  if (ym2612.OPN.ST.mode & 2)
    timer_b_next_oflow = (int)((double)(tbc - tbt) / (double)tbc * timer_b_step);
  else
    timer_b_next_oflow = TIMER_NO_OFLOW;
  elprintf(EL_YMTIMER, "load: %i/%i, timer_a_next_oflow %i", tat>>16, tac>>16, timer_a_next_oflow >> 8);
  elprintf(EL_YMTIMER, "load: %i/%i, timer_b_next_oflow %i", tbt>>16, tbc>>16, timer_b_next_oflow >> 8);
}

// -----------------------------------------------------------------
//                        z80 memhandlers

static unsigned char MEMH_FUNC z80_md_vdp_read(unsigned short a)
{
  // TODO?
  elprintf(EL_ANOMALY, "z80 invalid r8 [%06x] %02x", a, 0xff);
  return 0xff;
}

static unsigned char MEMH_FUNC z80_md_bank_read(unsigned short a)
{
  extern unsigned int PicoReadM68k8(unsigned int a);
  unsigned int addr68k;
  unsigned char ret;

  addr68k = Pico.m.z80_bank68k<<15;
  addr68k += a & 0x7fff;

  if (addr68k < Pico.romsize) {
    ret = Pico.rom[addr68k^1];
    goto out;
  }

  ret = m68k_read8(addr68k);
  elprintf(EL_ANOMALY, "z80->68k upper read [%06x] %02x", addr68k, ret);

out:
  elprintf(EL_Z80BNK, "z80->68k r8 [%06x] %02x", addr68k, ret);
  return ret;
}

static void MEMH_FUNC z80_md_ym2612_write(unsigned int a, unsigned char data)
{
  if (PicoOpt & POPT_EN_FM)
    emustatus |= ym2612_write_local(a, data, 1) & 1;
}

static void MEMH_FUNC z80_md_vdp_br_write(unsigned int a, unsigned char data)
{
  // TODO: allow full VDP access
  if ((a&0xfff9) == 0x7f11) // 7f11 7f13 7f15 7f17
  {
    if (PicoOpt & POPT_EN_PSG)
      SN76496Write(data);
    return;
  }

  if ((a>>8) == 0x60)
  {
    Pico.m.z80_bank68k >>= 1;
    Pico.m.z80_bank68k |= data << 8;
    Pico.m.z80_bank68k &= 0x1ff; // 9 bits and filled in the new top one
    return;
  }

  elprintf(EL_ANOMALY, "z80 invalid w8 [%06x] %02x", a, data);
}

static void MEMH_FUNC z80_md_bank_write(unsigned int a, unsigned char data)
{
  extern void PicoWriteM68k8(unsigned int a, unsigned char d);
  unsigned int addr68k;

  addr68k = Pico.m.z80_bank68k << 15;
  addr68k += a & 0x7fff;

  elprintf(EL_Z80BNK, "z80->68k w8 [%06x] %02x", addr68k, data);
  m68k_write8(addr68k, data);
}

// -----------------------------------------------------------------

static unsigned char z80_md_in(unsigned short p)
{
  elprintf(EL_ANOMALY, "Z80 port %04x read", p);
  return 0xff;
}

static void z80_md_out(unsigned short p, unsigned char d)
{
  elprintf(EL_ANOMALY, "Z80 port %04x write %02x", p, d);
}

static void z80_mem_setup(void)
{
  z80_map_set(z80_read_map, 0x0000, 0x1fff, Pico.zram, 0);
  z80_map_set(z80_read_map, 0x2000, 0x3fff, Pico.zram, 0);
  z80_map_set(z80_read_map, 0x4000, 0x5fff, ym2612_read_local_z80, 1);
  z80_map_set(z80_read_map, 0x6000, 0x7fff, z80_md_vdp_read, 1);
  z80_map_set(z80_read_map, 0x8000, 0xffff, z80_md_bank_read, 1);

  z80_map_set(z80_write_map, 0x0000, 0x1fff, Pico.zram, 0);
  z80_map_set(z80_write_map, 0x2000, 0x3fff, Pico.zram, 0);
  z80_map_set(z80_write_map, 0x4000, 0x5fff, z80_md_ym2612_write, 1);
  z80_map_set(z80_write_map, 0x6000, 0x7fff, z80_md_vdp_br_write, 1);
  z80_map_set(z80_write_map, 0x8000, 0xffff, z80_md_bank_write, 1);

#ifdef _USE_DRZ80
  drZ80.z80_in = z80_md_in;
  drZ80.z80_out = z80_md_out;
#endif
#ifdef _USE_CZ80
  Cz80_Set_Fetch(&CZ80, 0x0000, 0x1fff, (UINT32)Pico.zram); // main RAM
  Cz80_Set_Fetch(&CZ80, 0x2000, 0x3fff, (UINT32)Pico.zram); // mirror
  Cz80_Set_INPort(&CZ80, z80_md_in);
  Cz80_Set_OUTPort(&CZ80, z80_md_out);
#endif
}

