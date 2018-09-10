/*
 * (c) Copyright 2006-2010 notaz, All rights reserved.
 *
 * For performance reasons 3 renderers are exported for both MD and 32x modes:
 * - 16bpp line renderer
 * - 8bpp line renderer (slightly faster)
 * - 8bpp tile renderer
 * In 32x mode:
 * - 32x layer is overlayed on top of 16bpp one
 * - line internal one done on PicoDraw2FB, then mixed with 32x
 * - tile internal one done on PicoDraw2FB, then mixed with 32x
 */

#include "plat_ps2.h"

#include <kernel.h>
#include <audsrv.h>

#include "utils/ps2_config.h"
#include "utils/ps2_drawer.h"
#include "utils/ps2_pico.h"
#include "utils/ps2_sound.h"
#include "utils/ps2_textures.h"
#include "utils/ps2_timing.h"
#include "utils/ps2_semaphore.h"
#include "mp3.h"
#include "utils/asm.h"
#include "../common/plat.h"
#include "../common/emu.h"
#include "../common/menu.h"
#include "../common/config.h"
#include "../common/input.h"
#include "../common/lprintf.h"
#include <pico/pico_int.h>
#include <pico/cd/cue.h>

//Variables for the emulator core to use.
unsigned char *PicoDraw2FB;

extern void *_gp;

//############################# SOUND ####################################
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

static void ps2_SetAudioFormat(unsigned int rate) {
	//lprintf("FJTRUJY: ps2_SetAudioFormat\n");
    struct audsrv_fmt_t AudioFmt;
    
    AudioFmt.bits = 16;
    AudioFmt.freq = rate;
    AudioFmt.channels = 2;
    audsrv_set_format(&AudioFmt);
    audsrv_set_volume(MAX_VOLUME);
}

static void sound_thread(void *args) {
	//lprintf("FJTRUJY: sound_thread\n");
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

	// lprintf("sthr: exit\n");
	ExitDeleteThread();
}

static void ps2_soundInit(void) {
	//lprintf("FJTRUJY: sound_init\n");
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

static void writeSound(int len) {
	//lprintf("FJTRUJY: writeSound\n");
	if (isPicoOptStereoEnabled()) len<<=1;

	PsndOut += len;
	if (PsndOut >= sndBuffer_endptr)
		PsndOut = sndBuffer;

	// signal the snd thread
	samples_made += len;
	if (samples_made - samples_done > samples_block*2) {
		WakeupThread(sound_thread_id);
	}
}

static void ps2_soundStart(void) {
    static int PsndRate_old = 0, oldPicoOptFullAudioEnabled = 0, pal_old = 0;
    
    SuspendThread(sound_thread_id);
    
    samples_made = samples_done = 0;

	int psndRateChanged = PsndRate != PsndRate_old;
	int fullAudioChanged = isPicoOptFullAudioEnabled() != oldPicoOptFullAudioEnabled;
	int videoSystemChanged = Pico.m.pal != pal_old;
    
    if ( psndRateChanged || fullAudioChanged || videoSystemChanged ) {
        PsndRerate(Pico.m.frame_count ? 1 : 0);
    }
    
    samples_block = Pico.m.pal ? SOUND_BLOCK_SIZE_PAL : SOUND_BLOCK_SIZE_NTSC;
    if (PsndRate <= 11025) samples_block /= 4;
    else if (PsndRate <= 22050) samples_block /= 2;
    sndBuffer_endptr = &sndBuffer[samples_block*SOUND_BLOCK_COUNT];
    
    lprintf("starting audio: %i, len: %i, stereo: %i, pal: %i, block samples: %i\n",
            PsndRate, PsndLen, isPicoOptStereoEnabled(), Pico.m.pal, samples_block);
    
    PicoWriteSound = writeSound;
    memset(sndBuffer, 0, sizeof(sndBuffer));
    snd_playptr = sndBuffer_endptr - samples_block;
    samples_made = samples_block; // send 1 empty block first..
    PsndOut = sndBuffer;
    PsndRate_old = PsndRate;
    oldPicoOptFullAudioEnabled  = isPicoOptFullAudioEnabled();
    pal_old = Pico.m.pal;
    
    ps2_SetAudioFormat(PsndRate);
    
    sound_thread_stop=0;
    ResumeThread(sound_thread_id);
    WakeupThread(sound_thread_id);
}

static void ps2_soundStop(void) {
	sound_thread_stop=1;
    if (PsndOut != NULL) {
        PsndOut = NULL;
        WakeupThread(sound_thread_id);
    }
}

static void ps2_soundPrepare(void) {
	static int mp3_init_done = 0;

	if (PicoAHW & PAHW_MCD) {
        // mp3...
        if (!mp3_init_done) {
            int error;
            error = mp3_init();
            //lprintf("FJTRUJY: Que mierda me han devuelto %i\n", error);
            mp3_init_done = 1;
            if (error) { engineState = PGS_Menu; return; }
        }
    }
    
    // prepare sound stuff
    PsndOut = NULL;
    if (isSoundEnabled()) {
        ps2_soundStart();
    }
}

static void ps2_soundWait(void) {
	lprintf("pemu_sound_wait\n");
    // TODO: test this
    while (!sound_thread_exit && samples_made - samples_done > samples_block * 4)
        delayMS(10);
}

//############################  SOUND ##########################################

static int EmuScanSlow8(unsigned int num) {
	// lprintf("EmuScanSlow8\n");
	DrawLineDest = (unsigned char *)g_screen_ptr + num*frameBufferTexture->Width;

	return 0;
}

static int EmuScanSlow16(unsigned int num) {
	// lprintf("FJTRUJY: EmuScanSlow16\n");
	DrawLineDest = (unsigned short int *)g_screen_ptr + num*frameBufferTexture->Width;

	return 0;
}

static void setupFrameBufferTextureForEmulation(void) {

}

static void initFrameBufferTextureForEmulation(void) {
	if(is16Bits()) {
		//16-bit mode (No CLUT).
		frameBufferTexture->Clut=NULL;
	} else {
		//8-bit mode
		frameBufferTexture->Clut=memalign(128, gsKit_texture_size_ee(16, 16, frameBufferTexture->ClutPSM));
		frameBufferTexture->VramClut=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(16, 16, frameBufferTexture->ClutPSM), GSKIT_ALLOC_USERBUFFER);
	}

	gsKit_setup_tbw(frameBufferTexture);
	frameBufferTexture->Mem=memalign(128, gsKit_texture_size_ee(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM));
	frameBufferTexture->Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM), GSKIT_ALLOC_USERBUFFER);
	DrawLineDest=PicoDraw2FB=g_screen_ptr=(void*)((unsigned int)frameBufferTexture->Mem);
}

//Note: While this port has the CAN_HANDLE_240_LINES setting set, it seems like Picodrive will draw mandatory borders (of 320x8). Cutting them off by playing around with the pointers (see code below) should be harmless...
static void vidResetMode(void) {
    deinitFrameBufferTexture();
    
	clearGSGlobal();

	// bilinear filtering for the PSP and PS2.
	frameBufferTexture->Filter=(currentConfig.scaling)?GS_FILTER_LINEAR:GS_FILTER_NEAREST;

	if (is8BitsAccurate() || is16Bits()) {
		setPicoOptNormalRendered();

		lprintf("FJTRUJY: Accurate line renderer\n");
		frameBufferTexture->Width=SCREEN_WIDTH;
		// frameBufferTexture->Height=SCREEN_HEIGHT;	//NTSC = 224 lines, PAL = 240 lines. Only the draw region will be shown on-screen (320x224 or 320x240).
		frameBufferTexture->Height=(!(Pico.video.reg[1]&8))?224:240;	//NTSC = 224 lines, PAL = 240 lines. Only the draw region will be shown on-screen (320x224 or 320x240).
		// frameBufferTexture->Height=240;

		if (is16Bits()) {
			lprintf("FJTRUJY: Accurate 16bits\n");
			//16-bit mode
			PicoDrawSetOutFormat(PDF_RGB555, 0);
			PicoDrawSetOutBuf(g_screen_ptr, g_screen_width);
			PicoDrawSetCallbacks(EmuScanSlow16, NULL);

			frameBufferTexture->PSM=GS_PSM_CT16;
			//No CLUT.
		} else {
			lprintf("FJTRUJY: Accurate 8bits\n");
			// //8-bit mode
			PicoDrawSetOutFormat(PDF_8BIT, 0);
			PicoDrawSetOutBuf(g_screen_ptr, g_screen_width);
			PicoDrawSetCallbacks(EmuScanSlow8, NULL);

			frameBufferTexture->PSM=GS_PSM_T8;
			frameBufferTexture->ClutPSM=GS_PSM_CT16;
		}

	} else { 
		//8-bit fast (frame) renderer ((320+8)x(224+8+8), as directed by the comment within Draw2.h). Only the draw region will be shown on-screen (320x224 or 320x240).
		setPicoOptAlternativeRendered();
		lprintf("FJTRUJY: 8bits fast\n");
		PicoDrawSetOutFormat(PDF_NONE, 0);
		frameBufferTexture->Width=328;
		frameBufferTexture->Height=240;

		frameBufferTexture->PSM=GS_PSM_T8;
		frameBufferTexture->ClutPSM=GS_PSM_CT16;
	}

	setupFrameBufferTextureForEmulation();

	//update drawer config
	emuDrawerUpdateConfig();

	initFrameBufferTextureForEmulation();
	resetFrameBufferTexture();

	Pico.m.dirtyPal = 1;	//Since the VRAM for the CLUT has been reallocated, reupload it.
}

static void toogleRendererPicoConfig(int is_next) {
	change_renderer(is_next);
}

static void showRendererStatusMessage(void) {
	if (is16Bits()) {
		emu_status_msg("16bit accurate renderer");
		//lprintf("FJTRUJY: 16bit accurate renderer\n");
	} else if (is8bitsFast()) {
		emu_status_msg(" 8bit fast renderer");
		lprintf("8bit fast renderer\n");
	} else {
		emu_status_msg(" 8bit accurate renderer");
		//lprintf("FJTRUJY: 8bit accurate renderer");
	}
}

static void toogleRenderer(int is_next, int is_menu) {
	
    toogleRendererPicoConfig(is_next);
    
	// If we are in the menu, then we don't need to reset the video mode
	if (is_menu)
        return;
    
    vidResetMode();
    
    showRendererStatusMessage();
}

static void deinitFrameBufferTextureForEmulation(void) {
	frameBufferTexture->Width=0;
    frameBufferTexture->Height=0;
    frameBufferTexture->Mem=NULL;
    frameBufferTexture->Clut=NULL;
    PicoDraw2FB=NULL;
}

// All the PEMU, EMU, PLAT, ARM Methods

void spend_cycles(int c) {
	//lprintf("FJTRUJY: spend_cycles\n");
    delayCycles(c);
}

void emu_video_mode_change(int start_line, int line_count, int is_32cols){
    //lprintf("FJTRUJY: emu_video_mode_change\n");
	resetFrameBufferTexture();
}

void plat_video_toggle_renderer(int is_next, int is_menu) {
	//lprintf("FJTRUJY: plat_video_toggle_renderer\n");
	toogleRenderer(is_next, is_menu);
}

void pemu_prep_defconfig(void) {
	//lprintf("FJTRUJY: pemu_prep_defconfig\n");
    prepareDefaultConfig();
}

void pemu_validate_config(void) {
	//lprintf("FJTRUJY: pemu_validate_config\n");
}

void pemu_finalize_frame(const char *fps, const char *notice_msg) {
	//lprintf("FJTRUJY: pemu_finalize_frame\n");
    emuDrawerShowInfo(fps, notice_msg, 0);
}

void pemu_sound_start(void) {
	//lprintf("FJTRUJY: pemu_sound_start\n");
	ps2_soundStart();
}

void pemu_sound_stop(void) {
	//lprintf("FJTRUJY: pemu_sound_stop\n");
	ps2_soundStop();
}

/* wait until we can write more sound */
void pemu_sound_wait(void) {
	ps2_soundWait();
}

void pemu_forced_frame(int opts, int no_scale) {
	//lprintf("FJTRUJY: pemu_forced_frame\n");
    int po_old = currentPicoOpt();
    int eo_old = currentEmulationOpt();
    
	setPicoOptAccSprites();
	picoOptUpdateOpt(opts);
    
    vidResetMode();
    
    PicoFrameDrawOnly();
    
    setPicoOpt(po_old);
	updateEmulationOpt(eo_old);
}

void pemu_loop_prep(void) {
	//lprintf("FJTRUJY: pemu_loop_prep\n");
	deinitFrameBufferTextureForEmulation();
	emuDrawerPrepareConfig();

    ps2_soundInit();
    vidResetMode();
	ps2_soundPrepare();
}

void pemu_loop_end(void) {
	//lprintf("FJTRUJY: pemu_loop_end\n");
    ps2_soundStop();
    resetFrameBufferTexture();
}
