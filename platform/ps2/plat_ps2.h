#ifndef __PS2_H__
#define __PS2_H__

#include <gsKit.h>

#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00) // turn black GS Screen
#define GS_GREY GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00) // turn grey GS Screen

//Thread priorities (lower = higher priority)
#define MAIN_THREAD_PRIORITY    0x51
#define SOUND_THREAD_PRIORITY    0x50

/* video */
void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2);
void ps2_ClearFrameBuffer(void);
void ps2_ClearScreen(void);

/* emu */
void DelayThread(unsigned short int msec);
void FlipFBNoSync(void);
void SyncFlipFB(void);

#endif
