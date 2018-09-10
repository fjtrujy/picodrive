#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include "ps2_config.h"
#include "../../common/emu.h"
#include <pico/pico_int.h>

// #define EOPT_EN_SRAM      (1<<0)  -----> 0x1     --------> 1
// #define EOPT_SHOW_FPS     (1<<1)  -----> 0x2     --------> 2
// #define EOPT_EN_SOUND     (1<<2)  -----> 0x4     --------> 4
// #define EOPT_GZIP_SAVES   (1<<3)  -----> 0x8     --------> 8
// #define EOPT_MMUHACK      (1<<4)  -----> 0x10    --------> 16
// #define EOPT_NO_AUTOSVCFG (1<<5)  -----> 0x20    --------> 32
// // unused                 (1<<6)  -----> 0x40    --------> 64
// #define EOPT_16BPP        (1<<7)  -----> 0x80    --------> 128
// #define EOPT_RAM_TIMINGS  (1<<8)  -----> 0x100   --------> 256
// #define EOPT_CONFIRM_SAVE (1<<9)  -----> 0x200   --------> 512
// #define EOPT_EN_CD_LEDS   (1<<10) -----> 0x400   --------> 1024
// #define EOPT_CONFIRM_LOAD (1<<11) -----> 0x800   --------> 2048
// #define EOPT_A_SN_GAMMA   (1<<12) -----> 0x1000  --------> 4096
// #define EOPT_VSYNC        (1<<13) -----> 0x2000  --------> 8192
// #define EOPT_GIZ_SCANLN   (1<<14) -----> 0x4000  --------> 16384
// #define EOPT_GIZ_DBLBUF   (1<<15) -----> 0x8000  --------> 32768
// #define EOPT_VSYNC_MODE   (1<<16) -----> 0x10000 --------> 65536
// #define EOPT_SHOW_RTC     (1<<17) -----> 0x20000 --------> 131072
// #define EOPT_NO_FRMLIMIT  (1<<18) -----> 0x40000 --------> 262144
// #define EOPT_WIZ_TEAR_FIX (1<<19) -----> 0x80000 --------> 524288
// #define EOPT_EXT_FRMLIMIT (1<<20) -----> 0x100000--------> 1048576 // no internal frame limiter (limited by snd, etc)

const char *renderer_names[] = { "16bit accurate", " 8bit accurate", " 8bit fast", NULL };
const char *renderer_names32x[] = { "accurate", "faster", "fastest", NULL };
enum renderer_types { RT_16BIT, RT_8BIT_ACC, RT_8BIT_FAST, RT_COUNT };

// PUBLIC METHODS

int get_renderer(void) {
	if (PicoAHW & PAHW_32X) {
        return currentConfig.renderer32x;
    } else {
		return currentConfig.renderer;
    }
}

int currentEmulationOpt(void) {
    return currentConfig.EmuOpt;
}

int isShowFPSEnabled(void) {
    return (currentConfig.EmuOpt & EOPT_SHOW_FPS);
}

int isSoundEnabled(void) {
    return (currentConfig.EmuOpt & EOPT_EN_SOUND);
}

int is8BitsAccurate(void) {
    return (get_renderer() == RT_8BIT_ACC);
}

int is8bitsFast(void) {
    return (get_renderer() == RT_8BIT_FAST);
}

int is16Bits(void) {
    return (get_renderer() == RT_16BIT || (PicoAHW & PAHW_32X));
}

int isCDLedsEnabled(void) {
    return (currentConfig.EmuOpt & EOPT_EN_CD_LEDS);
}

int isVSYNCEnabled(void) {
    return (currentConfig.EmuOpt & EOPT_VSYNC);
}

int isVSYNCModeEnabled(void) {
    return (currentConfig.EmuOpt & EOPT_VSYNC_MODE);
}

void prepareDefaultConfig(void) {
    defaultConfig.EmuOpt = EOPT_EN_SRAM | EOPT_EN_SOUND | EOPT_GZIP_SAVES | EOPT_MMUHACK | EOPT_CONFIRM_SAVE | EOPT_EN_CD_LEDS;
    defaultConfig.renderer = RT_8BIT_FAST; // for now is just working with 8bit fast
    defaultConfig.renderer32x = RT_8BIT_FAST;
}

void updateEmulationOpt(int newEmuOpt) {
    currentConfig.EmuOpt = newEmuOpt;
}

void change_renderer(int diff) {
	int *r;
	if (PicoAHW & PAHW_32X) {
        r = &currentConfig.renderer32x;
    } else {
        r = &currentConfig.renderer;
    }
		
	*r += diff;
	if (*r >= RT_COUNT) {
        *r = 0;
    } else if (*r < 0) {
        *r = RT_COUNT - 1;
    }
}
