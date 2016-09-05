// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <sys/stat.h>
#include <sys/types.h>

#include <kernel.h>
#include <fileXio_rpc.h>
#include <gsKit.h>
#include <libpad.h>
#include <audsrv.h>
#include <unistd.h>
#include <limits.h>

#include "ps2.h"
#include "menu.h"
#include "emu.h"
#include "mp3.h"
#include "asm_utils.h"
#include "../common/emu.h"
#include "../common/config.h"
#include "../common/lprintf.h"
#include <pico/pico_int.h>
#include <pico/cd/cue.h>

#define OSD_FPS_X 270	//OSD FPS indicator X-coordinate.

//Palette options (Don't change them, since these values might be hardcoded without the use of these definitions). They're listed here to show that they exist.
#define OSD_STAT_BLK_PAL_ENT		0xE0	//OSD black colour palette entry (Used as the background for OSD messages and the CD status LEDs).
#define OSD_TXT_PAL_ENT			0xF0	//OSD text palette entry.
#define OSD_CD_STAT_GREEN_PAL_EN	0xC0	//OSD CD status green LED palette entry
#define OSD_CD_STAT_RED_PAL_EN		0xD0	//OSD CD status red LED palette entry

//Variables for the emulator core to use.
extern void *ps2_screen;
extern unsigned short int ps2_screen_draw_width, ps2_screen_draw_height;
extern GSTEXTURE FrameBufferTexture;
unsigned char *PicoDraw2FB;

extern GSGLOBAL *gsGlobal;
extern void *_gp;

char romFileName[PATH_MAX];
int engineState = PGS_Menu;

static int combo_keys = 0, combo_acts = 0; // keys and actions which need button combos
static unsigned int noticeMsgTime = 0;
int reset_timing = 0; // do we need this?

#define PICO_PEN_ADJUST_X 4
#define PICO_PEN_ADJUST_Y 2
static short int pico_pen_x = 320/2, pico_pen_y = 240/2;

static unsigned short int FrameBufferTextureVisibleWidth, FrameBufferTextureVisibleHeight;
static unsigned short int FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleOffsetY;	//From upper left-hand corner.

static void sound_init(void);
static void sound_deinit(void);
static void blit(const char *fps, const char *notice, int lagging_behind);

void emu_noticeMsgUpdated(void)
{
	noticeMsgTime = ps2_GetTicksInUsec();
}

int emu_getMainDir(char *dst, int len)
{
	if (len > 0) *dst = 0;
	return 0;
}

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
			unsigned char *screen_8 = ps2_screen;
			memset(&screen_8[x+FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(ScreenHeight-h-1)], OSD_STAT_BLK_PAL_ENT, len);
		}
		emu_textOut8(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
	else{
		//16-bit mode
		for (h = 8; h >= 0; h--) {
			int pixel_w;
			for(pixel_w=0; pixel_w<len; pixel_w++){
				unsigned short int *screen_16=ps2_screen;
				screen_16[x+pixel_w+FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(ScreenHeight-h-1)]=0x8000;
			}
		}
		emu_textOut16(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
}

void emu_msg_cb(const char *msg)
{
	osd_text(4, msg);
	noticeMsgTime =  ps2_GetTicksInUsec() - 2000000;

	/* assumption: emu_msg_cb gets called only when something slow is about to happen */
	reset_timing = 1;
}

static void emu_msg_tray_open(void)
{
	strcpy(noticeMsg, "CD tray opened");
	noticeMsgTime = ps2_GetTicksInUsec();
}

void emu_Init(void)
{
	// make dirs for saves, cfgs, etc.
	ps2_mkdir("mds", 0777);
	ps2_mkdir("srm", 0777);
	ps2_mkdir("brm", 0777);
	ps2_mkdir("cfg", 0777);

	FrameBufferTexture.Width=0;
	FrameBufferTexture.Height=0;
	FrameBufferTextureVisibleOffsetX=0;
	FrameBufferTextureVisibleOffsetY=0;
	FrameBufferTextureVisibleWidth=0;
	FrameBufferTextureVisibleHeight=0;
	FrameBufferTexture.Mem=NULL;
	FrameBufferTexture.Clut=NULL;
	PicoDraw2FB=NULL;

	sound_init();

	PicoInit();
	PicoMessage = emu_msg_cb;
	PicoMCDopenTray = emu_msg_tray_open;
	PicoMCDcloseTray = menu_loop_tray;
}

void emu_Deinit(void)
{
	// save SRAM
	if ((currentConfig.EmuOpt & 1) && SRam.changed) {
		emu_SaveLoadGame(0, 1);
		SRam.changed = 0;
	}

	if (!(currentConfig.EmuOpt & 0x20))
		config_writelrom(PicoConfigFile);

	PicoExit();
	sound_deinit();
	if(FrameBufferTexture.Mem!=NULL){
		free(FrameBufferTexture.Mem);
		FrameBufferTexture.Mem=NULL;
	}
	if(FrameBufferTexture.Clut!=NULL){
		free(FrameBufferTexture.Clut);
		FrameBufferTexture.Clut=NULL;
	}
}

void emu_prepareDefaultConfig(void)
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
	for(i=0; i<4; i++){
		defaultConfig.JoyBinds[i][ 4] = 1<<0; // SACB RLDU
		defaultConfig.JoyBinds[i][ 6] = 1<<1;
		defaultConfig.JoyBinds[i][ 7] = 1<<2;
		defaultConfig.JoyBinds[i][ 5] = 1<<3;
		defaultConfig.JoyBinds[i][14] = 1<<4;
		defaultConfig.JoyBinds[i][13] = 1<<5;
		defaultConfig.JoyBinds[i][15] = 1<<6;
		defaultConfig.JoyBinds[i][ 3] = 1<<7;
		defaultConfig.JoyBinds[i][12] = 1<<26; // switch rnd
		defaultConfig.JoyBinds[i][ 8] = 1<<27; // save state
		defaultConfig.JoyBinds[i][ 9] = 1<<28; // load state
		defaultConfig.JoyBinds[i][28] = 1<<0; // num "buttons"
		defaultConfig.JoyBinds[i][30] = 1<<1;
		defaultConfig.JoyBinds[i][31] = 1<<2;
		defaultConfig.JoyBinds[i][29] = 1<<3;
	}
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
	DrawLineDest = (unsigned char *)ps2_screen + num*FrameBufferTexture.Width;

	return 0;
}

static int EmuScanSlow16(unsigned int num)
{
	DrawLineDest = (unsigned short int *)ps2_screen + num*FrameBufferTexture.Width;

	return 0;
}

static void cd_leds(void)
{
	unsigned int reg, col_g, col_r;

	reg = Pico_mcd->s68k_regs[0];

	if (!(currentConfig.EmuOpt&0x80)) {
		// 8-bit modes
		col_g = (reg & 2) ? 0xc0c0c0c0 : 0xe0e0e0e0;
		col_r = (reg & 1) ? 0xd0d0d0d0 : 0xe0e0e0e0;
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(3+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(4+FrameBufferTextureVisibleOffsetY)+ 4) = col_g;
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(3+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(4+FrameBufferTextureVisibleOffsetY)+12) = col_r;
	} else {
		// 16-bit modes
		unsigned int *p = (unsigned int *)((short *)ps2_screen + FrameBufferTextureVisibleOffsetX+FrameBufferTexture.Width*(2+FrameBufferTextureVisibleOffsetY)+4);
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
	unsigned char *p = (unsigned char *)ps2_screen;

	// only if pen enabled and for 8bit mode
	if (pico_inp_mode == 0 || (currentConfig.EmuOpt&0x80)) return;

	p += 512 * (pico_pen_y + PICO_PEN_ADJUST_Y);
	p += pico_pen_x + PICO_PEN_ADJUST_X;
	p[  -1] = 0xe0; p[   0] = 0xf0; p[   1] = 0xe0;
	p[ 511] = 0xf0; p[ 512] = 0xf0; p[ 513] = 0xf0;
	p[1023] = 0xe0; p[1024] = 0xf0; p[1025] = 0xe0;
}

void emu_platformDebugCat(char *str)
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
		ps2_screen_width=FrameBufferTexture.Width=320;
		ps2_screen_height=FrameBufferTexture.Height=(!(Pico.video.reg[1]&8))?224:240;	//NTSC = 224 lines, PAL = 240 lines. Only the draw region will be shown on-screen (320x224 or 320x240).
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

		ps2_screen_width=FrameBufferTexture.Width=328;
		ps2_screen_height=FrameBufferTexture.Height=240;
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
	DrawLineDest=PicoDraw2FB=ps2_screen=(void*)((unsigned int)FrameBufferTexture.Mem);

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

void emu_startSound(void)
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

void emu_endSound(void)
{
	sound_thread_stop=1;
	WakeupThread(sound_thread_id);
}

/* wait until we can write more sound */
void emu_waitSound(void)
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

void emu_forcedFrame(int opts)
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

		if ( emu_checkSaveFile(state_slot) &&
				(( (which & 0x1000) && (currentConfig.EmuOpt & 0x800)) || // load
				 (!(which & 0x1000) && (currentConfig.EmuOpt & 0x200))) ) // save
		{
			int keys;
			blit("", (which & 0x1000) ? "LOAD STATE? (X=yes, O=no)" : "OVERWRITE SAVE? (X=yes, O=no)", 0);
			while( !((keys = ps2_pad_read_all()) & (PBTN_X|PBTN_CIRCLE)) ) {};
			if (keys & PBTN_CIRCLE) do_it = 0;
			while(  ((keys = ps2_pad_read_all()) & (PBTN_X|PBTN_CIRCLE)) ) {};// wait for release
		}

		if (do_it)
		{
			osd_text(4, (which & 0x1000) ? "LOADING GAME" : "SAVING GAME");
			emu_draw(0);
			PicoStateProgressCB = emu_msg_cb;
			emu_SaveLoadGame((which & 0x1000) >> 12, 0);
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

		if (PicoOpt&0x10) {
			strcpy(noticeMsg, "8bit fast renderer");
		} else if (currentConfig.EmuOpt&0x80) {
			strcpy(noticeMsg, "16bit accurate renderer");
		} else {
			strcpy(noticeMsg, "8bit accurate renderer");
		}

		noticeMsgTime = ps2_GetTicksInUsec();
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
		sprintf(noticeMsg, "SAVE SLOT %i [%s]", state_slot, emu_checkSaveFile(state_slot) ? "USED" : "FREE");
		noticeMsgTime = ps2_GetTicksInUsec();
	}
}

static void updateKeys(void)
{
	unsigned int keys[2], allActions[2] = { 0, 0 }, events;
	static unsigned int prevEvents = 0;
	int i, player_idx;

	keys[0] = ps2_pad_read(0, 0);
	keys[1] = ps2_pad_read(1, 0);

	if ((keys[0]|keys[1]) & PBTN_SELECT)
		engineState = PGS_Menu;

	keys[0] &= CONFIGURABLE_KEYS;
	keys[1] &= CONFIGURABLE_KEYS;

	for (i = 0; i < 32; i++)
	{
		for(player_idx=0; player_idx<2; player_idx++){
			if (keys[player_idx] & (1 << i))
			{
				int acts = currentConfig.JoyBinds[player_idx][i];
				if (!acts) continue;
				if (combo_keys & (1 << i))
				{
					int u = i+1, acts_c = acts & combo_acts;
					// let's try to find the other one
					if (acts_c)
						for (; u < 32; u++)
							if ( (currentConfig.JoyBinds[player_idx][u] & acts_c) && (keys[player_idx] & (1 << u)) ) {
								allActions[player_idx] |= acts_c;
								keys[player_idx] &= ~((1 << i) | (1 << u));
								break;
							}
					// add non-combo actions if combo ones were not found
					if (!acts_c || u == 32)
						allActions[player_idx] |= acts & ~combo_acts;
				} else {
					allActions[player_idx] |= acts;
				}
			}
		}
	}

	PicoPad[0] = allActions[0] & 0xfff;
	PicoPad[1] = allActions[1] & 0xfff;

	if (allActions[0] & 0x7000) emu_DoTurbo(&PicoPad[0], allActions[0]);
	if (allActions[1] & 0x7000) emu_DoTurbo(&PicoPad[1], allActions[1]);

	events = (allActions[0] | allActions[1]) >> 16;

	if ((events ^ prevEvents) & 0x40) {
		emu_changeFastForward(events & 0x40);
		reset_timing = 1;
	}

	events &= ~prevEvents;

	if (PicoAHW == PAHW_PICO)
		RunEventsPico(events, keys[0]|keys[1]);
	if (events) RunEvents(events);
	if (movie_data) emu_updateMovie();

	prevEvents = (allActions[0] | allActions[1]) >> 16;
}

static void find_combos(int player_idx)
{
	int act, u;

	// find out which keys and actions are combos
	combo_keys = combo_acts = 0;
	for (act = 0; act < 32; act++)
	{
		int keyc = 0, keyc2 = 0;
		if (act == 16 || act == 17) continue; // player2 flag
		if (act > 17)
		{
			for (u = 0; u < 24; u++) // 24, because nub can't produce combos
				if (currentConfig.JoyBinds[player_idx][u] & (1 << act)) keyc++;
		}
		else
		{
			for (u = 0; u < 24; u++)
				if (currentConfig.JoyBinds[player_idx][u] & (1 << act)) keyc++;
			for (u = 0; u < 24; u++)
				if (currentConfig.JoyBinds[player_idx][u] & (1 << act)) keyc2++;
		}
		if (keyc > 1 || keyc2 > 1)
		{
			// loop again and mark those keys and actions as combo
			for (u = 0; u < 24; u++)
			{
				if (currentConfig.JoyBinds[player_idx][u] & (1 << act)) {
					combo_keys |= 1 << u;
					combo_acts |= 1 << act;
				}
			}
		}
	}
}

void emu_Loop(void)
{
	static int mp3_init_done = 0;
	char fpsbuff[24]; // fps count c string
	unsigned int tval, tval_prev = 0, tval_thissec = 0; // timing
	int frames_done = 0, frames_shown = 0, oldmodes = 0;
	int target_fps, target_frametime, lim_time, tval_diff, i;
	char *notice = NULL;

	lprintf("entered emu_Loop()\n");

	fpsbuff[0] = 0;

	// make sure we are in correct mode
	vidResetMode();
	oldmodes = ((Pico.video.reg[12]&1)<<2) ^ 0xc;
	find_combos(0);
	find_combos(1);

	// pal/ntsc might have changed, reset related stuff
	target_fps = Pico.m.pal ? 50 : 60;
	target_frametime = Pico.m.pal ? (1000000<<8)/50 : (1000000<<8)/60+1;
	reset_timing = 1;

	if (PicoAHW & PAHW_MCD) {
		// prepare CD buffer
		PicoCDBufferInit();
		// mp3...
		if (!mp3_init_done) {
			i = mp3_init();
			mp3_init_done = 1;
			if (i) { engineState = PGS_Menu; return; }
		}
	}

	// prepare sound stuff
	PsndOut = NULL;
	if (currentConfig.EmuOpt & 4)
	{
        emu_startSound();
	}

	// loop?
	while (engineState == PGS_Running)
	{
		int modes;

		tval = ps2_GetTicksInUsec();
		if (reset_timing || tval < tval_prev) {
			//stdbg("timing reset");
			reset_timing = 0;
			tval_thissec = tval;
			frames_shown = frames_done = 0;
		}

		// show notice message?
		if (noticeMsgTime) {
			static int noticeMsgSum;
			if (tval - noticeMsgTime > 2000000) { // > 2.0 sec
				noticeMsgTime = 0;
				notice = 0;
			} else {
				int sum = noticeMsg[0]+noticeMsg[1]+noticeMsg[2];
				if (sum != noticeMsgSum) { noticeMsgSum = sum; }
				notice = noticeMsg;
			}
		}

		// check for mode changes
		modes = ((Pico.video.reg[12]&1)<<2)|(Pico.video.reg[1]&8);
		if (modes != oldmodes) {
			oldmodes = modes;
			ps2_ClearScreen();
		}

		// second passed?
		if (tval - tval_thissec >= 1000000)
		{
			// missing 1 frame?
			if (currentConfig.Frameskip < 0 && frames_done < target_fps) {
				SkipFrame(); frames_done++;
			}

			if (currentConfig.EmuOpt & 2)
				sprintf(fpsbuff, "%02i/%02i", frames_shown, frames_done);

			tval_thissec += 1000000;

			if (currentConfig.Frameskip < 0) {
				frames_done  -= target_fps; if (frames_done  < 0) frames_done  = 0;
				frames_shown -= target_fps; if (frames_shown < 0) frames_shown = 0;
				if (frames_shown > frames_done) frames_shown = frames_done;
			} else {
				frames_done = frames_shown = 0;
			}
		}
#ifdef PFRAMES
		sprintf(fpsbuff, "%i", Pico.m.frame_count);
#endif

		//Decide on whether frameskipping is required.
		tval_prev = tval;
		lim_time = (frames_done+1) * target_frametime;
		if (currentConfig.Frameskip >= 0) // frameskip enabled
		{
			for (i = 0; i < currentConfig.Frameskip; i++) {
				updateKeys();
				SkipFrame(); frames_done++;
				if (!(currentConfig.EmuOpt&0x40000)) { // do framelimitting if needed
					if((tval = ps2_GetTicksInUsec())<tval_thissec){	//Counter overflow.
						tval_diff=(((UINT_MAX-tval)+tval)-tval_thissec) << 8;
					}
					else{
						tval_diff = (int)(tval - tval_thissec) << 8;
					}
					if (tval_diff < lim_time) // we are too fast
						DelayThread(((lim_time - tval_diff)>>8)/1000);
				}
				lim_time += target_frametime;
			}
		}
		else // auto frameskip
		{
			if((tval = ps2_GetTicksInUsec())<tval_thissec){	//Counter overflow.
				tval_diff=(((UINT_MAX-tval)+tval)-tval_thissec) << 8;
			}
			else{
				tval_diff = (int)(tval - tval_thissec) << 8;
			}
			if (tval_diff > lim_time && (frames_done/16 < frames_shown))
			{
				// no time left for this frame - skip
				if (tval_diff - lim_time >= (300000<<8)) {
					reset_timing = 1;
					continue;
				}
				updateKeys();
				SkipFrame(); frames_done++;
				continue;
			}
		}

		updateKeys();
		PicoFrame();

		// check time
		if((tval = ps2_GetTicksInUsec())<tval_thissec){	//Counter overflow.
			tval_diff=(((UINT_MAX-tval)+tval)-tval_thissec) << 8;
		}
		else{
			tval_diff = (int)(tval - tval_thissec) << 8;
		}

		blit(fpsbuff, notice, tval_diff > lim_time);

		//Perform frame limiting.
		if (currentConfig.Frameskip < 0 && tval_diff - lim_time >= (300000<<8)) // slowdown detection
			reset_timing = 1;
		else if (!(currentConfig.EmuOpt&0x40000) || currentConfig.Frameskip < 0)
		{
			// sleep if we are still too fast
			if (tval_diff < lim_time)
			{
				// we are too fast
				DelayThread(((lim_time - tval_diff)>>8)/1000);
			}
		}

		frames_done++; frames_shown++;
	}

	emu_changeFastForward(0);

	if (PicoAHW & PAHW_MCD) PicoCDBufferFree();

	if (PsndOut != NULL) {
		PsndOut = NULL;
        emu_endSound();
	}

	// save SRAM
	if ((currentConfig.EmuOpt & 1) && SRam.changed) {
		emu_msg_cb("Writing SRAM/BRAM..");
		emu_SaveLoadGame(0, 1);
		SRam.changed = 0;
	}

	// clear fps counters and stuff
	ps2_ClearScreen();
}

void emu_ResetGame(void)
{
	PicoReset();
	reset_timing = 1;
}
