#include "ps2_pico.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include <pico/pico_int.h>

#include "ps2_pico.h"

// // Pico.c
// #define POPT_EN_FM          (1<< 0) -----> 0x1     --------> 1
// #define POPT_EN_PSG         (1<< 1) -----> 0x2     --------> 2
// #define POPT_EN_Z80         (1<< 2) -----> 0x4     --------> 4
// #define POPT_EN_STEREO      (1<< 3) -----> 0x8     --------> 8
// #define POPT_ALT_RENDERER   (1<< 4) -----> 0x10    --------> 16
// #define POPT_6BTN_PAD       (1<< 5) -----> 0x20    --------> 32
// // unused                   (1<< 6) -----> 0x40    --------> 64
// #define POPT_ACC_SPRITES    (1<< 7) -----> 0x80    --------> 128
// #define POPT_DIS_32C_BORDER (1<< 8) -----> 0x100   --------> 256
// #define POPT_EXT_FM         (1<< 9) -----> 0x200   --------> 512
// #define POPT_EN_MCD_PCM     (1<<10) -----> 0x400   --------> 1024
// #define POPT_EN_MCD_CDDA    (1<<11) -----> 0x800   --------> 2048
// #define POPT_EN_MCD_GFX     (1<<12) -----> 0x1000  --------> 4096
// #define POPT_EN_MCD_PSYNC   (1<<13) -----> 0x2000  --------> 8192
// #define POPT_EN_SOFTSCALE   (1<<14) -----> 0x4000  --------> 16384
// #define POPT_EN_MCD_RAMCART (1<<15) -----> 0x8000  --------> 32768
// #define POPT_DIS_VDP_FIFO   (1<<16) -----> 0x10000 --------> 65536
// #define POPT_EN_SVP_DRC     (1<<17) -----> 0x20000 --------> 131072
// #define POPT_DIS_SPRITE_LIM (1<<18) -----> 0x40000 --------> 262144
// #define POPT_DIS_IDLE_DET   (1<<19) -----> 0x80000 --------> 524288
// #define POPT_EN_32X         (1<<20) -----> 0x100000--------> 1048576
// #define POPT_EN_PWM         (1<<21) -----> 0x200000--------> 2097152

// Private Methods

static int isPicoOptFMEnabled(void) {
    return (PicoOpt & POPT_EN_FM);
}

static int isPicoOptPSGEnabled(void) {
    return (PicoOpt & POPT_EN_PSG);
}

// Public Methods

int currentPicoOpt(void) {
    return PicoOpt;
}

int isPicoOptMCDCDDAEnabled(void) {
    return (PicoOpt & POPT_EN_MCD_CDDA);
}

int isPicoOptStereoEnabled(void) {
    return (PicoOpt & POPT_EN_STEREO);
}

int isPicoOptFullAudioEnabled(void) {
    int fullAudioEnabled = POPT_EN_FM | POPT_EN_PSG | POPT_EN_STEREO;
    return (PicoOpt & fullAudioEnabled); 
}

void setPicoOptNormalRendered(void) {
    PicoOpt &= ~POPT_ALT_RENDERER;
}

void setPicoOptAlternativeRendered(void) {
    PicoOpt |= POPT_ALT_RENDERER;
}

void setPicoOptAccSprites(void) {
    PicoOpt |= POPT_ACC_SPRITES;
}

void setPicoOpt(int newOpt) {
    PicoOpt = newOpt;
}

void picoOptUpdateOpt(int optToUpdate) {
    PicoOpt |= optToUpdate;
}
