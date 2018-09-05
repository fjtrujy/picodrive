// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include "plat_ps2.h"

#include <kernel.h>
#include <audsrv.h>

#include "utils/ps2_config.h"
#include "utils/ps2_drawer.h"
#include "utils/ps2_pico.h"
#include "utils/ps2_textures.h"
#include "utils/ps2_timing.h"
#include "utils/ps2_semaphore.h"
#include "mp3.h"
#include "utils/asm.h"
#include "../common/plat.h"
#include "../common/menu.h"
#include "../common/config.h"
#include "../common/input.h"
#include "../common/lprintf.h"
#include <pico/pico_int.h>
#include <pico/cd/cue.h>

//Variables for the emulator core to use.
unsigned char *PicoDraw2FB;

extern void *_gp;

static int EmuScanSlow8(unsigned int num) {
	// lprintf("EmuScanSlow8\n");
	DrawLineDest = (unsigned char *)g_screen_ptr + num*frameBufferTexture->Width;

	return 0;
}

static int EmuScanSlow16(unsigned int num) {
	lprintf("EmuScanSlow16\n");
	DrawLineDest = (unsigned short int *)g_screen_ptr + num*frameBufferTexture->Width;

	return 0;
}

void spend_cycles(int c) {
	lprintf("spend_cycles\n");
    delayCycles(c);
}

//Note: While this port has the CAN_HANDLE_240_LINES setting set, it seems like Picodrive will draw mandatory borders (of 320x8). Cutting them off by playing around with the pointers (see code below) should be harmless...
static void vidResetMode(void) {
	lprintf("vidResetMode: vmode: %s, renderer: %s (%u-bit mode)\n", (Pico.video.reg[1])&8?"PAL":"NTSC", isPicoOptAlternativeRenderedEnabled()?"Fast":"Accurate", is8BitsConfig()?8:16);
    deinitFrameBufferTexture();
    
	clearGSGlobal();

	// bilinear filtering for the PSP and PS2.
	frameBufferTexture->Filter=(currentConfig.scaling)?GS_FILTER_LINEAR:GS_FILTER_NEAREST;

	if(!isPicoOptAlternativeRenderedEnabled()){	//Accurate (line) renderer.
		frameBufferTexture->Width=SCREEN_WIDTH;
		frameBufferTexture->Height=(!(Pico.video.reg[1]&8))?224:240;	//NTSC = 224 lines, PAL = 240 lines. Only the draw region will be shown on-screen (320x224 or 320x240).

		if(is8BitsConfig()){
			//8-bit mode
			PicoDrawSetColorFormat(2);
			PicoScanBegin = &EmuScanSlow8;
			PicoScanEnd = NULL;

			frameBufferTexture->PSM=GS_PSM_T8;
			frameBufferTexture->ClutPSM=GS_PSM_CT16;
		}
		else{
			//16-bit mode
			PicoDrawSetColorFormat(1);
			PicoScanBegin = &EmuScanSlow16;
			PicoScanEnd = NULL;

			frameBufferTexture->PSM=GS_PSM_CT16;
			//No CLUT.
		}
	} else {	//8-bit fast (frame) renderer ((320+8)x(224+8+8), as directed by the comment within Draw2.h). Only the draw region will be shown on-screen (320x224 or 320x240).
		frameBufferTexture->Width=328;
		frameBufferTexture->Height=240;

		frameBufferTexture->PSM=GS_PSM_T8;
		frameBufferTexture->ClutPSM=GS_PSM_CT16;
	}

	//update drawer config
	emuDrawerUpdateConfig();

	if(is8BitsConfig()){
		//8-bit mode
		frameBufferTexture->Clut=memalign(128, gsKit_texture_size_ee(16, 16, frameBufferTexture->ClutPSM));
		frameBufferTexture->VramClut=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(16, 16, frameBufferTexture->ClutPSM), GSKIT_ALLOC_USERBUFFER);
	}
	else{
		//16-bit mode (No CLUT).
		frameBufferTexture->Clut=NULL;
	}

	gsKit_setup_tbw(frameBufferTexture);
	frameBufferTexture->Mem=memalign(128, gsKit_texture_size_ee(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM));
	frameBufferTexture->Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM), GSKIT_ALLOC_USERBUFFER);
	DrawLineDest=PicoDraw2FB=g_screen_ptr=(void*)((unsigned int)frameBufferTexture->Mem);

	resetFrameBufferTexture();

	Pico.m.dirtyPal = 1;	//Since the VRAM for the CLUT has been reallocated, reupload it.
}

void emu_video_mode_change(int start_line, int line_count, int is_32cols){
    lprintf("emu_video_mode_change\n");
	resetFrameBufferTexture();
}

void plat_video_toggle_renderer(int is_next, int force_16bpp, int is_menu) {
	lprintf("plat_video_toggle_renderer\n");
    if (force_16bpp) {
        setPicoOptNormalRendered();
        set16BtisConfig();
    }
    /* alt, 16bpp, 8bpp */
    else if (isPicoOptAlternativeRenderedEnabled()) {
        setPicoOptNormalRendered();
        if (is_next)
            set16BtisConfig();
    } else if (is8BitsConfig()) {
        if (is_next)
            setPicoOptAlternativeRendered();
        else
            set16BtisConfig();
    } else {
        set8BtisConfig();
        if (!is_next)
            setPicoOptAlternativeRendered();
    }
    
    if (is_menu)
        return;
    
    vidResetMode();
    
    if (isPicoOptAlternativeRenderedEnabled()) {
        emu_status_msg(" 8bit fast renderer");
		lprintf("8bit fast renderer\n");
    } else if (is16BitsAccurate()) {
        emu_status_msg("16bit accurate renderer");
		lprintf("16bit accurate renderer\n");
    } else {
        emu_status_msg(" 8bit accurate renderer");
		lprintf("8bit accurate renderer");
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

void ps2_SetAudioFormat(unsigned int rate) {
	lprintf("ps2_SetAudioFormat\n");
    struct audsrv_fmt_t AudioFmt;
    
    AudioFmt.bits = 16;
    AudioFmt.freq = rate;
    AudioFmt.channels = 2;
    audsrv_set_format(&AudioFmt);
    audsrv_set_volume(MAX_VOLUME);
}

void sound_thread(void *args) {
	lprintf("sound_thread\n");
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

		lprintf("sthr: got data: %i\n", samples_made - samples_done);

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
static void sound_init(void) {
	lprintf("sound_init\n");
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
	lprintf("writeSound\n");
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

// All the PEMU Methods

void pemu_prep_defconfig(void) {
	lprintf("pemu_prep_defconfig\n");
    prepareDefaultConfig();
}

void pemu_validate_config(void) {
	lprintf("pemu_validate_config\n");
}

void pemu_finalize_frame(const char *fps, const char *notice_msg) {
	lprintf("pemu_finalize_frame\n");
    emuDrawerShowInfo(fps, notice_msg, 0);
}

void pemu_sound_start(void) {
	lprintf("pemu_sound_start\n");
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

void pemu_sound_stop(void) {
	lprintf("pemu_sound_stop\n");
    sound_thread_stop=1;
    if (PsndOut != NULL) {
        PsndOut = NULL;
        WakeupThread(sound_thread_id);
    }
}

/* wait until we can write more sound */
void pemu_sound_wait(void) {
	lprintf("pemu_sound_wait\n");
    // TODO: test this
    while (!sound_thread_exit && samples_made - samples_done > samples_block * 4)
        delayMS(10);
}

void pemu_forced_frame(int opts) {
	lprintf("pemu_forced_frame\n");
    int po_old = currentPicoOpt();
    int eo_old = currentEmulationOpt();
    
    setPicoOptNormalRendered();
	setPicoOptAccSprites();
	picoOptUpdateOpt(opts);
    set16BtisConfig();
    
    vidResetMode();
    
    PicoFrameDrawOnly();
    
    setPicoOpt(po_old);
	updateEmulationOpt(eo_old);
}

void pemu_loop_prep(void) {
	lprintf("pemu_loop_prep\n");
    static int mp3_init_done = 0;
    
	frameBufferTexture->Width=0;
    frameBufferTexture->Height=0;
    frameBufferTexture->Mem=NULL;
    frameBufferTexture->Clut=NULL;
    PicoDraw2FB=NULL;

	emuDrawerPrepareConfig();

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
    if (isSoundEnabled()) {
        pemu_sound_start();
    }
}

void pemu_loop_end(void) {
	lprintf("pemu_loop_end\n");
    pemu_sound_stop();
    resetFrameBufferTexture();
}
