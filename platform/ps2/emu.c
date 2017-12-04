// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include "plat_ps2.h"

#include <kernel.h>
#include <audsrv.h>

#include "mp3.h"
#include "asm_utils.h"
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

//Variables for the emulator core to use.
extern GSTEXTURE FrameBufferTexture;
unsigned char *PicoDraw2FB;

extern GSGLOBAL *gsGlobal;
extern void *_gp;

static unsigned short int FrameBufferTextureVisibleWidth, FrameBufferTextureVisibleHeight;
static unsigned short int FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleOffsetY;	//From upper left-hand corner.

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

void spend_cycles(int c)
{
    DelayThread(c/295);
}

void ps2_memcpy_all_buffers(void *data, int offset, int len)
{
    char *dst = (char *)data + offset;
    if (dst != data) memcpy(dst, data, len);
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

//Note: While this port has the CAN_HANDLE_240_LINES setting set, it seems like Picodrive will draw mandatory borders (of 320x8). Cutting them off by playing around with the pointers (see code below) should be harmless...
static void vidResetMode(void)
{
//	lprintf("vidResetMode: vmode: %s, renderer: %s (%u-bit mode)\n", (Pico.video.reg[1])&8?"PAL":"NTSC", (PicoOpt&0x10)?"Fast":"Accurate", !(currentConfig.EmuOpt&0x80)?8:16);
    ps2_ClearFrameBuffer();
    
	gsKit_clear(gsGlobal, GS_BLACK);

	// bilinear filtering for the PSP and PS2.
	FrameBufferTexture.Filter=(currentConfig.scaling)?GS_FILTER_LINEAR:GS_FILTER_NEAREST;

	if(!(PicoOpt&0x10)){	//Accurate (line) renderer.
		FrameBufferTextureVisibleOffsetX=0;	//Nothing to hide here.
		FrameBufferTextureVisibleOffsetY=0;
		FrameBufferTexture.Width=SCREEN_WIDTH;
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

void emu_video_mode_change(int start_line, int line_count, int is_32cols)
{
    ps2_ClearScreen();
}

void plat_video_toggle_renderer(int is_next, int force_16bpp, int is_menu)
{
    if (force_16bpp) {
        PicoOpt &= ~POPT_ALT_RENDERER;
        currentConfig.EmuOpt |= EOPT_16BPP;
    }
    /* alt, 16bpp, 8bpp */
    else if (PicoOpt & POPT_ALT_RENDERER) {
        PicoOpt &= ~POPT_ALT_RENDERER;
        if (is_next)
            currentConfig.EmuOpt |= EOPT_16BPP;
    } else if (!(currentConfig.EmuOpt & EOPT_16BPP)) {
        if (is_next)
            PicoOpt |= POPT_ALT_RENDERER;
        else
            currentConfig.EmuOpt |= EOPT_16BPP;
    } else {
        currentConfig.EmuOpt &= ~EOPT_16BPP;
        if (!is_next)
            PicoOpt |= POPT_ALT_RENDERER;
    }
    
    if (is_menu)
        return;
    
    vidResetMode();
    
    if (PicoOpt & POPT_ALT_RENDERER) {
        emu_status_msg(" 8bit fast renderer");
    } else if (currentConfig.EmuOpt & EOPT_16BPP) {
        emu_status_msg("16bit accurate renderer");
    } else {
        emu_status_msg(" 8bit accurate renderer");
    }
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
static unsigned char sound_thread_stack[0xA00] ALIGNED(128);

void ps2_SetAudioFormat(unsigned int rate){
    struct audsrv_fmt_t AudioFmt;
    
    AudioFmt.bits = 16;
    AudioFmt.freq = rate;
    AudioFmt.channels = 2;
    audsrv_set_format(&AudioFmt);
    audsrv_set_volume(MAX_VOLUME);
}

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

// All the PEMU Methods

void pemu_prep_defconfig(void)
{
    defaultConfig.EmuOpt    = 0x1d | 0x600; // | <- confirm_save, cd_leds, 8-bit acc rend
}

void pemu_validate_config(void)
{
}

void pemu_finalize_frame(const char *fps, const char *notice_msg)
{
    blit(fps, notice_msg, 0);
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
    if (PsndOut != NULL) {
        PsndOut = NULL;
        WakeupThread(sound_thread_id);
    }
}

/* wait until we can write more sound */
void pemu_sound_wait(void)
{
    // TODO: test this
    while (!sound_thread_exit && samples_made - samples_done > samples_block * 4)
        DelayThread(10);
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

void pemu_loop_prep(void)
{
    static int mp3_init_done = 0;
    
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

    vidResetMode();
    
    if (PicoAHW & PAHW_MCD) {
        // mp3...
        if (!mp3_init_done) {
            int error;
            error = mp3_init();
            lprintf("Que mierda me han devuelto %i\n", error);
            mp3_init_done = 1;
            if (error) { engineState = PGS_Menu; return; }
        }
    }
    
    // prepare sound stuff
    PsndOut = NULL;
    if (currentConfig.EmuOpt & 4)
    {
        pemu_sound_start();
    }
}

void pemu_loop_end(void)
{
    pemu_sound_stop();
    ps2_ClearScreen();
}
