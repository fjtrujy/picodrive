#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <audsrv.h>

#include "ps2_config.h"
#include "ps2_pico.h"
#include "ps2_sound.h"
#include "ps2_timing.h"
#include "../mp3.h"
#include "../plat_ps2.h"

#include <pico/pico_int.h>
#include <pico/cd/cue.h>
#include "../../common/emu.h"

// TODO: THIS CLASS NEED A REFACTOR
// Extract every use of the config for Pico or sound different class
// Make easier to read where the pointers/constants used for the audio
// Remove the values and constants in the file for something with more sense.

// Priavate constants/macros
#define SOUND_BLOCK_SIZE_NTSC (1470*2) // 1024 // 1152
#define SOUND_BLOCK_SIZE_PAL  (1764*2)
#define SOUND_BLOCK_COUNT    8

static short __attribute__((aligned(64))) sndBuffer[SOUND_BLOCK_SIZE_PAL*SOUND_BLOCK_COUNT + 44100/50*2];
static short *snd_playptr = NULL, *sndBuffer_endptr = NULL;
static int samples_made = 0, samples_done = 0, samples_block = 0;
static unsigned char sound_thread_exit = 0, sound_thread_stop = 0;
static int sound_thread_id = -1;
static unsigned char sound_thread_stack[0xA00] ALIGNED(128);

// Looks to be needed because is used by the linkfile
extern void *_gp;

// Private Methods

void emuSetAudioFormat(unsigned int rate) {
	lprintf("emuSetAudioFormat\n");
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
		if (snd_playptr >= sndBuffer_endptr) {
			snd_playptr = sndBuffer;
		}

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
static void emuSoundInit(void) {
	lprintf("emuSoundInit\n");
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
	
	if (sound_thread_id >= 0) {
		ret = StartThread(sound_thread_id, NULL);
		if (ret < 0) {
			lprintf("emuSoundInit: StartThread returned %d\n", ret);
		}
	} else {
		lprintf("CreateThread failed: %d\n", sound_thread_id);
	}
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

// Public Methods

void emuSoundPrepare(void) {
    static int mp3_init_done = 0;
    emuSoundInit();
    
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
        emuSoundStart();
    }
}

void emuSoundStart(void) {
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
    
    emuSetAudioFormat(PsndRate);
    
    sound_thread_stop=0;
    ResumeThread(sound_thread_id);
    WakeupThread(sound_thread_id);
}

void emuSoundStop(void) {
    sound_thread_stop=1;
    if (PsndOut != NULL) {
        PsndOut = NULL;
        WakeupThread(sound_thread_id);
    }
}

void emuSoundWait(void) {
    // TODO: test this
    while (!sound_thread_exit && samples_made - samples_done > samples_block * 4)
        delayMS(10);
}