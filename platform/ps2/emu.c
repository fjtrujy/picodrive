// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include "plat_ps2.h"

#include <kernel.h>
#include <fileXio_rpc.h>
#include <gsKit.h>
#include <libpad.h>
#include <audsrv.h>
#include <unistd.h>
#include <limits.h>

#include "emu.h"
#include "mp3.h"
#include "asm_utils.h"
#include "version.h"
#include "../common/plat.h"
#include "../common/menu.h"
#include "../common/emu.h"
#include "../common/config.h"
#include "../common/input.h"
#include "../common/lprintf.h"
#include <pico/pico_int.h>
#include <pico/cd/cue.h>

#define OSD_FPS_X 270	//OSD FPS indicator X-coordinate.

//Palette options (Don't change them, since these values might be hardcoded without the use of these definitions). They're listed here to show that they exist.
#define OSD_STAT_BLK_PAL_ENT		0xE0	//OSD black colour palette entry (Used as the background for OSD messages and the CD status LEDs).
#define OSD_TXT_PAL_ENT			0xF0	//OSD text palette entry.
#define OSD_CD_STAT_GREEN_PAL_EN	0xC0	//OSD CD status green LED palette entry
#define OSD_CD_STAT_RED_PAL_EN		0xD0	//OSD CD status red LED palette entry
#define SOUND_THREAD_PRIORITY    0x50

//Variables for the emulator core to use.
extern GSTEXTURE FrameBufferTexture;
unsigned char *PicoDraw2FB;

extern GSGLOBAL *gsGlobal;
extern void *_gp;

static int combo_keys = 0, combo_acts = 0; // keys and actions which need button combos

#define PICO_PEN_ADJUST_X 4
#define PICO_PEN_ADJUST_Y 2

static unsigned short int FrameBufferTextureVisibleWidth, FrameBufferTextureVisibleHeight;
static unsigned short int FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleOffsetY;	//From upper left-hand corner.

static void sound_init(void);
static void sound_deinit(void);
static void blit(const char *fps, const char *notice, int lagging_behind);

static void emu_draw(int lagging_behind){
	// want vsync?
	if((currentConfig.EmuOpt & 0x2000) && (!(currentConfig.EmuOpt & 0x10000) || !lagging_behind)){
		SyncFlipFB();
	}
	else{
		FlipFBNoSync();
	}
}

static void osd_text(int x, const char *text)
{
	unsigned short int ScreenHeight;
	int len = strlen(text) * 8;
	char h;

	ScreenHeight=FrameBufferTextureVisibleHeight+FrameBufferTextureVisibleOffsetY;
	if(!(currentConfig.EmuOpt&0x80)){
		//8-bit mode
		for (h = 8; h>=0; h--) {
			unsigned char *screen_8 = g_screen_ptr;
			memset(&screen_8[x+FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(ScreenHeight-h-1)], OSD_STAT_BLK_PAL_ENT, len);
		}
		emu_text_out8(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
	else{
		//16-bit mode
		for (h = 8; h >= 0; h--) {
			int pixel_w;
			for(pixel_w=0; pixel_w<len; pixel_w++){
				unsigned short int *screen_16=g_screen_ptr;
				screen_16[x+pixel_w+FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(ScreenHeight-h-1)]=0x8000;
			}
		}
		emu_text_out16(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
}

void emu_msg_cb(const char *msg)
{
	osd_text(4, msg);

	/* assumption: emu_msg_cb gets called only when something slow is about to happen */
	reset_timing = 1;
}

void pemu_prep_defconfig(void)
{
	unsigned int i;

	memset(&defaultConfig, 0, sizeof(defaultConfig));
	defaultConfig.EmuOpt    = 0x1d | 0x600; // | <- confirm_save, cd_leds, 8-bit acc rend
	defaultConfig.s_PicoOpt = 0x0f | POPT_EN_MCD_PCM|POPT_EN_MCD_CDDA|POPT_EN_MCD_GFX|POPT_ACC_SPRITES;
	defaultConfig.s_PsndRate = 44100;
	defaultConfig.s_PicoRegion = 0; // auto
	defaultConfig.s_PicoAutoRgnOrder = 0x184; // US, EU, JP
	defaultConfig.s_PicoCDBuffers = 64;
	defaultConfig.Frameskip = -1; // auto
}

static inline void do_pal_update(void)
{
	int i;
	unsigned short int *pal=(void *)FrameBufferTexture.Clut;
	//Megadrive palette:	0000BBB0GGG0RRR0
	//16-bit clut palette:	ABBBBBGGGGGRRRRR

	if(Pico.video.reg[0xC]&8){
		do_pal_convert_with_shadows(pal, Pico.cram);
	}
	else{
		do_pal_convert(pal, Pico.cram);
		if(rendstatus & PDRAW_SPR_LO_ON_HI) memcpy(&pal[0x80], pal, 0x40*2);
	}
	//For OSD messages and status indicators.
	pal[OSD_STAT_BLK_PAL_ENT] = 0x8000;
	pal[OSD_TXT_PAL_ENT] = 0xFFFF;
	pal[OSD_CD_STAT_GREEN_PAL_EN] = 0x83E0;
	pal[OSD_CD_STAT_RED_PAL_EN] = 0x801F;

  	//Rotate CLUT.
	for (i = 0; i < 256; i++)
	{
		if ((i&0x18) == 8)
		{
			unsigned short int tmp = pal[i];
			pal[i] = pal[i+8];
			pal[i+8] = tmp;
		}
	}

	Pico.m.dirtyPal = 0;
}

static void blitscreen_clut(void)
{
	if(Pico.m.dirtyPal){
		do_pal_update();

		SyncDCache(FrameBufferTexture.Clut, (void*)((unsigned int)FrameBufferTexture.Clut+256*2));
		gsKit_texture_send_inline(gsGlobal, FrameBufferTexture.Clut, 16, 16, FrameBufferTexture.VramClut, FrameBufferTexture.ClutPSM, 1, GS_CLUT_PALLETE);	// upload 16*16 entries (256)
	}
}

extern void *DrawLineDest;

static int EmuScanSlow8(unsigned int num)
{
	DrawLineDest = (unsigned char *)g_screen_ptr + num*FrameBufferTexture.Width;

	return 0;
}

static int EmuScanSlow16(unsigned int num)
{
	DrawLineDest = (unsigned short int *)g_screen_ptr + num*FrameBufferTexture.Width;

	return 0;
}

void pemu_update_display(const char *fps, const char *notice)
{
    blit(fps, notice, 0);
}

unsigned int plat_get_ticks_ms(void)
{
    return plat_get_ticks_us()/1000;
}

unsigned int plat_get_ticks_us(void)
{
    return ps2_GetTicksInUsec();
}

void spend_cycles(int c)
{
    DelayThread(c/295);
}

void plat_wait_till_us(unsigned int us_to)
{
    unsigned int now;
    
    spend_cycles(1024);
    now = plat_get_ticks_us();
    
    while ((signed int)(us_to - now) > 512)
    {
        spend_cycles(1024);
        now = plat_get_ticks_us();
    }
}

void plat_video_wait_vsync(void)
{
}

void plat_status_msg_clear(void)
{
    int is_8bit = (PicoOpt & POPT_ALT_RENDERER) || !(currentConfig.EmuOpt & EOPT_16BPP);
    if (currentConfig.EmuOpt & EOPT_WIZ_TEAR_FIX) {
        /* ugh.. */
        int i, u, *p;
        if (is_8bit) {
            p = (int *)g_screen_ptr + (240-8) / 4;
            for (u = 320; u > 0; u--, p += 240/4)
                p[0] = p[1] = 0xe0e0e0e0;
        } else {
            p = (int *)g_screen_ptr + (240-8)*2 / 4;
            for (u = 320; u > 0; u--, p += 240*2/4)
                p[0] = p[1] = p[2] = p[3] = 0;
        }
        return;
    }
    
    if (is_8bit)
        ps2_memset_all_buffers(320*232, 0xe0, 320*8);
    else
        ps2_memset_all_buffers(320*232*2, 0, 320*8*2);
}

void plat_status_msg_busy_next(const char *msg)
{
    plat_status_msg_clear();
    pemu_update_display("", msg);
    emu_status_msg("");
    
    /* assumption: msg_busy_next gets called only when
     * something slow is about to happen */
    reset_timing = 1;
}

void plat_status_msg_busy_first(const char *msg)
{
    ps2_memcpy_all_buffers(g_screen_ptr, 0, 320*240*2);
    plat_status_msg_busy_next(msg);
}

void plat_update_volume(int has_changed, int is_up)
{
}

static void cd_leds(void)
{
    unsigned int reg, col_g, col_r;

	reg = Pico_mcd->s68k_regs[0];

	if (!(currentConfig.EmuOpt&0x80)) {
		// 8-bit modes
		col_g = (reg & 2) ? 0xc0c0c0c0 : 0xe0e0e0e0;
		col_r = (reg & 1) ? 0xd0d0d0d0 : 0xe0e0e0e0;
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(3+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(4+FrameBufferTextureVisibleOffsetY)+ 4) = col_g;
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(3+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(4+FrameBufferTextureVisibleOffsetY)+12) = col_r;
	} else {
		// 16-bit modes
		unsigned int *p = (unsigned int *)((short *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+4);
		unsigned int col_g = (reg & 2) ? 0x83008300 : 0x80008000;
		unsigned int col_r = (reg & 1) ? 0x80188018 : 0x80008000;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += FrameBufferTexture.Width/2 - 12/2;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += FrameBufferTexture.Width/2 - 12/2;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r;
	}
}

static void draw_pico_ptr(void);

static void blit(const char *fps, const char *notice, int lagging_behind)
{
	if (notice)      osd_text(4, notice);
	if (currentConfig.EmuOpt & 2) osd_text(OSD_FPS_X, fps);

	if ((currentConfig.EmuOpt & 0x400) && (PicoAHW & PAHW_MCD))
		cd_leds();
	if (PicoAHW & PAHW_PICO)
		draw_pico_ptr();

	if(!(currentConfig.EmuOpt&0x80)) blitscreen_clut();

	SyncDCache(FrameBufferTexture.Mem, (void*)((unsigned int)FrameBufferTexture.Mem+gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM)));
	gsKit_texture_send_inline(gsGlobal, FrameBufferTexture.Mem, FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.Vram, FrameBufferTexture.PSM, FrameBufferTexture.TBW, GS_CLUT_TEXTURE); //Use GS_CLUT_TEXTURE for PSM_T8.
	ps2_DrawFrameBuffer(FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleOffsetY, FrameBufferTextureVisibleWidth+FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleHeight+FrameBufferTextureVisibleOffsetY);

	emu_draw(lagging_behind);
}

static void draw_pico_ptr(void)
{
	unsigned char *p = (unsigned char *)g_screen_ptr;

	// only if pen enabled and for 8bit mode
	if (pico_inp_mode == 0 || (currentConfig.EmuOpt&0x80)) return;

	p += 512 * (pico_pen_y + PICO_PEN_ADJUST_Y);
	p += pico_pen_x + PICO_PEN_ADJUST_X;
	p[  -1] = 0xe0; p[   0] = 0xf0; p[   1] = 0xe0;
	p[ 511] = 0xf0; p[ 512] = 0xf0; p[ 513] = 0xf0;
	p[1023] = 0xe0; p[1024] = 0xf0; p[1025] = 0xe0;
}

void plat_debug_cat(char *str)
{
	strcat(str, (currentConfig.EmuOpt&0x80) ? "soft clut\n" : "hard clut\n");	//TODO: is this valid for this port?
}

//Note: While this port has the CAN_HANDLE_240_LINES setting set, it seems like Picodrive will draw mandatory borders (of 320x8). Cutting them off by playing around with the pointers (see code below) should be harmless...
static void vidResetMode(void)
{
//	lprintf("vidResetMode: vmode: %s, renderer: %s (%u-bit mode)\n", (Pico.video.reg[1])&8?"PAL":"NTSC", (PicoOpt&0x10)?"Fast":"Accurate", !(currentConfig.EmuOpt&0x80)?8:16);

	if(FrameBufferTexture.Mem!=NULL) free(FrameBufferTexture.Mem);
	if(FrameBufferTexture.Clut!=NULL){
		free(FrameBufferTexture.Clut);
		FrameBufferTexture.Clut=NULL;
	}
	gsKit_vram_clear(gsGlobal);
	gsKit_clear(gsGlobal, GS_BLACK);

	// bilinear filtering for the PSP and PS2.
	FrameBufferTexture.Filter=(currentConfig.scaling)?GS_FILTER_LINEAR:GS_FILTER_NEAREST;

	if(!(PicoOpt&0x10)){	//Accurate (line) renderer.
		FrameBufferTextureVisibleOffsetX=0;	//Nothing to hide here.
		FrameBufferTextureVisibleOffsetY=0;
		FrameBufferTexture.Width=320;
		FrameBufferTexture.Height=(!(Pico.video.reg[1]&8))?224:240;	//NTSC = 224 lines, PAL = 240 lines. Only the draw region will be shown on-screen (320x224 or 320x240).
		FrameBufferTextureVisibleWidth=FrameBufferTexture.Width;
		FrameBufferTextureVisibleHeight=FrameBufferTexture.Height;

		if(!(currentConfig.EmuOpt&0x80)){
			//8-bit mode
			PicoDrawSetColorFormat(2);
			PicoScanBegin = &EmuScanSlow8;
			PicoScanEnd = NULL;

			FrameBufferTexture.PSM=GS_PSM_T8;
			FrameBufferTexture.ClutPSM=GS_PSM_CT16;
		}
		else{
			//16-bit mode
			PicoDrawSetColorFormat(1);
			PicoScanBegin = &EmuScanSlow16;
			PicoScanEnd = NULL;

			FrameBufferTexture.PSM=GS_PSM_CT16;
			//No CLUT.
		}
	}
	else{	//8-bit fast (frame) renderer ((320+8)x(224+8+8), as directed by the comment within Draw2.h). Only the draw region will be shown on-screen (320x224 or 320x240).
		FrameBufferTextureVisibleOffsetX=8;

		//Skip borders.
		if(!(Pico.video.reg[1]&8)){	//NTSC.
			FrameBufferTextureVisibleOffsetY=8;	//NTSC has a shorter screen than PAL has.
			FrameBufferTextureVisibleHeight=224;
		}
		else{	//PAL
			FrameBufferTextureVisibleOffsetY=0;
			FrameBufferTextureVisibleHeight=240;
		}

		FrameBufferTexture.Width=328;
		FrameBufferTexture.Height=240;
		FrameBufferTextureVisibleWidth=320;

		FrameBufferTexture.PSM=GS_PSM_T8;
		FrameBufferTexture.ClutPSM=GS_PSM_CT16;
	}

	if(!(currentConfig.EmuOpt&0x80)){
		//8-bit mode
		FrameBufferTexture.Clut=memalign(128, gsKit_texture_size_ee(16, 16, FrameBufferTexture.ClutPSM));
		FrameBufferTexture.VramClut=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(16, 16, FrameBufferTexture.ClutPSM), GSKIT_ALLOC_USERBUFFER);
	}
	else{
		//16-bit mode (No CLUT).
		FrameBufferTexture.Clut=NULL;
	}

	gsKit_setup_tbw(&FrameBufferTexture);
	FrameBufferTexture.Mem=memalign(128, gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM));
	FrameBufferTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM), GSKIT_ALLOC_USERBUFFER);
	DrawLineDest=PicoDraw2FB=g_screen_ptr=(void*)((unsigned int)FrameBufferTexture.Mem);

	ps2_ClearScreen();

	Pico.m.dirtyPal = 1;	//Since the VRAM for the CLUT has been reallocated, reupload it.
}

/* sound stuff */
#define SOUND_BLOCK_SIZE_NTSC (1470*2) // 1024 // 1152
#define SOUND_BLOCK_SIZE_PAL  (1764*2)
#define SOUND_BLOCK_COUNT    8

static short __attribute__((aligned(64))) sndBuffer[SOUND_BLOCK_SIZE_PAL*SOUND_BLOCK_COUNT + 44100/50*2];
static short *snd_playptr = NULL, *sndBuffer_endptr = NULL;
static int samples_made = 0, samples_done = 0, samples_block = 0;
static unsigned char sound_thread_exit = 0, sound_thread_stop = 0;
static int sound_thread_id = -1;

static void writeSound(int len);

static void sound_thread(void *args)
{
	while (!sound_thread_exit)
	{
		if(sound_thread_stop){
			audsrv_stop_audio();
			samples_made = samples_done = 0;
			SleepThread();
			continue;
		}

		if (samples_made - samples_done < samples_block) {
			// wait for data (use at least 2 blocks)
			//lprintf("sthr: wait... (%i)\n", samples_made - samples_done);
			while (samples_made - samples_done <= samples_block*2 && (!sound_thread_exit && !sound_thread_stop)) SleepThread();
			continue;
		}

		audsrv_wait_audio(samples_block*2);

		// lprintf("sthr: got data: %i\n", samples_made - samples_done);

		audsrv_play_audio((void*)snd_playptr, samples_block*2);

		samples_done += samples_block;
		snd_playptr  += samples_block;
		if (snd_playptr >= sndBuffer_endptr)
			snd_playptr = sndBuffer;

		// shouln't happen, but just in case
		if (samples_made - samples_done >= samples_block*3) {
			//lprintf("sthr: block skip (%i)\n", samples_made - samples_done);
			samples_done += samples_block; // skip
			snd_playptr  += samples_block;
		}
	}

	lprintf("sthr: exit\n");
	ExitDeleteThread();
}

static unsigned char sound_thread_stack[0xA00] ALIGNED(128);

static void sound_init(void)
{
	int ret;
	ee_thread_t thread;

	samples_made = samples_done = 0;
	samples_block = SOUND_BLOCK_SIZE_NTSC; // make sure it goes to sema
	sound_thread_exit = 0;
	thread.func=&sound_thread;
	thread.stack=sound_thread_stack;
	thread.stack_size=sizeof(sound_thread_stack);
	thread.gp_reg=&_gp;
	thread.initial_priority=SOUND_THREAD_PRIORITY;
	thread.attr=thread.option=0;
	sound_thread_id = CreateThread(&thread);
	if (sound_thread_id >= 0)
	{
		ret = StartThread(sound_thread_id, NULL);
		if (ret < 0) lprintf("sound_init: StartThread returned %d\n", ret);
	}
	else
		lprintf("CreateThread failed: %d\n", sound_thread_id);
}

void pemu_sound_start(void)
{
	static int PsndRate_old = 0, PicoOpt_old = 0, pal_old = 0;
	int stereo;

	SuspendThread(sound_thread_id);

	samples_made = samples_done = 0;

	if (PsndRate != PsndRate_old || (PicoOpt&0x0b) != (PicoOpt_old&0x0b) || Pico.m.pal != pal_old) {
		PsndRerate(Pico.m.frame_count ? 1 : 0);
	}
	stereo=(PicoOpt&8)>>3;

	samples_block = Pico.m.pal ? SOUND_BLOCK_SIZE_PAL : SOUND_BLOCK_SIZE_NTSC;
	if (PsndRate <= 11025) samples_block /= 4;
	else if (PsndRate <= 22050) samples_block /= 2;
	sndBuffer_endptr = &sndBuffer[samples_block*SOUND_BLOCK_COUNT];

	lprintf("starting audio: %i, len: %i, stereo: %i, pal: %i, block samples: %i\n",
			PsndRate, PsndLen, stereo, Pico.m.pal, samples_block);

	PicoWriteSound = writeSound;
	memset(sndBuffer, 0, sizeof(sndBuffer));
	snd_playptr = sndBuffer_endptr - samples_block;
	samples_made = samples_block; // send 1 empty block first..
	PsndOut = sndBuffer;
	PsndRate_old = PsndRate;
	PicoOpt_old  = PicoOpt;
	pal_old = Pico.m.pal;

	ps2_SetAudioFormat(PsndRate);

	sound_thread_stop=0;
	ResumeThread(sound_thread_id);
	WakeupThread(sound_thread_id);
}

void pemu_sound_stop(void)
{
	sound_thread_stop=1;
	WakeupThread(sound_thread_id);
}

/* wait until we can write more sound */
void pemu_sound_wait(void)
{
	// TODO: test this
	while (!sound_thread_exit && samples_made - samples_done > samples_block * 4)
		DelayThread(10);
}

static void sound_deinit(void)
{
	sound_thread_exit = 1;
	WakeupThread(sound_thread_id);
	sound_thread_id = -1;
}

static void writeSound(int len)
{
	if (PicoOpt&8) len<<=1;

	PsndOut += len;
	if (PsndOut >= sndBuffer_endptr)
		PsndOut = sndBuffer;

	// signal the snd thread
	samples_made += len;
	if (samples_made - samples_done > samples_block*2) {
		WakeupThread(sound_thread_id);
	}
}

static void SkipFrame(void)
{
	PicoSkipFrame=1;
	PicoFrame();
	PicoSkipFrame=0;
}

void pemu_forced_frame(int opts)
{
	int po_old = PicoOpt;
	int eo_old = currentConfig.EmuOpt;

	PicoOpt &= ~0x10;
	PicoOpt |= opts|POPT_ACC_SPRITES;
	currentConfig.EmuOpt |= 0x80;

	vidResetMode();

	PicoFrameDrawOnly();

	PicoOpt = po_old;
	currentConfig.EmuOpt = eo_old;
}

void plat_video_toggle_renderer(int is_next, int is_menu)
{
}

static void RunEventsPico(unsigned int events, unsigned int keys)
{
	emu_RunEventsPico(events);

	if (pico_inp_mode != 0)
	{
		PicoPad[0] &= ~0x0f; // release UDLR
		if (keys & PBTN_UP)   { pico_pen_y--; if (pico_pen_y < 8) pico_pen_y = 8; }
		if (keys & PBTN_DOWN) { pico_pen_y++; if (pico_pen_y > 224-PICO_PEN_ADJUST_Y) pico_pen_y = 224-PICO_PEN_ADJUST_Y; }
		if (keys & PBTN_LEFT) { pico_pen_x--; if (pico_pen_x < 0) pico_pen_x = 0; }
		if (keys & PBTN_RIGHT) {
			int lim = (Pico.video.reg[12]&1) ? 319 : 255;
			pico_pen_x++;
			if (pico_pen_x > lim-PICO_PEN_ADJUST_X)
				pico_pen_x = lim-PICO_PEN_ADJUST_X;
		}
		PicoPicohw.pen_pos[0] = pico_pen_x;
		if (!(Pico.video.reg[12]&1)) PicoPicohw.pen_pos[0] += pico_pen_x/4;
		PicoPicohw.pen_pos[0] += 0x3c;
		PicoPicohw.pen_pos[1] = pico_inp_mode == 1 ? (0x2f8 + pico_pen_y) : (0x1fc + pico_pen_y);
	}
}

static void RunEvents(unsigned int which)
{
	if (which & 0x1800) // save or load (but not both)
	{
		int do_it = 1;
        if ( emu_check_save_file(state_slot) &&
				(( (which & 0x1000) && (currentConfig.EmuOpt & 0x800)) || // load
				 (!(which & 0x1000) && (currentConfig.EmuOpt & 0x200))) ) // save
		{
			int keys;
			blit("", (which & 0x1000) ? "LOAD STATE? (X=yes, O=no)" : "OVERWRITE SAVE? (X=yes, O=no)", 0);
			while( !((keys = ps2_pad_read_all()) & (PBTN_MBACK|PBTN_MOK)) ) {};
			if (keys & PBTN_MOK) do_it = 0;
			while(  ((keys = ps2_pad_read_all()) & (PBTN_MBACK|PBTN_MOK)) ) {};// wait for release
		}

		if (do_it)
		{
			osd_text(4, (which & 0x1000) ? "LOADING GAME" : "SAVING GAME");
			emu_draw(0);
			PicoStateProgressCB = emu_msg_cb;
			emu_save_load_game((which & 0x1000) >> 12, 0);
			PicoStateProgressCB = NULL;
		}

		reset_timing = 1;
	}
	if (which & 0x0400) // switch renderer
	{
#ifndef ALLOW_16B_RENDERER_USE
		if (PicoOpt&0x10) { PicoOpt&=~0x10; }
		else              { PicoOpt|= 0x10; }
		currentConfig.EmuOpt &= ~0x80;
#else
		if      (  PicoOpt&0x10)             { PicoOpt&=~0x10; currentConfig.EmuOpt |= 0x80; }
		else if (!(currentConfig.EmuOpt&0x80)) PicoOpt|= 0x10;
		else   currentConfig.EmuOpt &= ~0x80;
#endif

		vidResetMode();

        if (PicoOpt & POPT_ALT_RENDERER) {
            emu_status_msg("fast renderer");
		} else if (currentConfig.EmuOpt&0x80) {
            emu_status_msg("accurate renderer");
		} else {
			emu_status_msg("8bit accurate renderer");
		}

	}
	if (which & 0x0300)
	{
		if(which&0x0200) {
			state_slot -= 1;
			if(state_slot < 0) state_slot = 9;
		} else {
			state_slot += 1;
			if(state_slot > 9) state_slot = 0;
		}
        
        emu_status_msg("SAVE SLOT %i [%s]", state_slot,
        emu_check_save_file(state_slot) ? "USED" : "FREE");
	}
}

void pemu_video_mode_change(int is_32col, int is_240_lines)
{
}

void pemu_loop_prep(void)
{
}

void pemu_loop_end(void)
{
}

const char *plat_get_credits(void)
{
    return "PicoDrive v" VERSION " (c) notaz, 06-09\n\n"
    "Returned life by fjtrujy (thanks sp193)\n/n"
    "Credits:\n"
    "fDave: Cyclone 68000 core,\n"
    "      base code of PicoDrive\n"
    "Reesy & FluBBa: DrZ80 core\n"
    "MAME devs: YM2612 and SN76496 cores\n"
    "rlyeh and others: minimal SDK\n"
    "Squidge: mmuhack\n"
    "Dzz: ARM940 sample\n"
    "\n"
    "Special thanks (for docs, ideas):\n"
    " Charles MacDonald, Haze,\n"
    " Stephane Dallongeville,\n"
    " Lordus, Exophase, Rokas,\n"
    " Nemesis, Tasco Deluxe";
}
