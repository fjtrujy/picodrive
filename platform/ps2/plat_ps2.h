#ifndef __PS2_H__
#define __PS2_H__

#include <gsKit.h>

#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00)

//Thread priorities (lower = higher priority)
#define MAIN_THREAD_PRIORITY    0x51
#define SOUND_THREAD_PRIORITY    0x50

/* video */
void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2);
void ps2_ClearFrameBuffer(void);
void ps2_ClearScreen(void);

/* emu */
u32 ps2_GetTicksInUsec(void);
void DelayThread(unsigned short int msec);
void FlipFBNoSync(void);
void SyncFlipFB(void);
void ps2_SetAudioFormat(unsigned int rate);

#define ALLOW_16B_RENDERER_USE    1    //Uncomment to allow users to select the 16-bit accurate renderer. It has no real purpose (Other than for taking screencaps), since it's slower than the 8-bit renderers.

#endif
