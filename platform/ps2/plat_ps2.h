#ifndef __PS2_H__
#define __PS2_H__

#include <gsKit.h>

#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00)

//extern int default_cpu_clock;
extern GSGLOBAL *gsGlobal;

//Thread priorities (lower = higher priority)
#define MAIN_THREAD_PRIORITY    0x51
#define SOUND_THREAD_PRIORITY    0x50

/* video */
void ps2_video_changemode(int bpp);
void ps2_memcpy_all_buffers(void *data, int offset, int len);
void ps2_memset_all_buffers(int offset, int byte, int len);
void ps2_make_fb_bufferable(int yes);

void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2);
void ps2_ClearScreen(void);

/* input */
int ps2_touchpad_read(int *x, int *y);

/* emu */
u32 ps2_GetTicksInUsec(void);
void DelayThread(unsigned short int msec);
void FlipFBNoSync(void);
void SyncFlipFB(void);
void ps2_SetAudioFormat(unsigned int rate);
unsigned int ps2_pad_read_all(void);

#define ALLOW_16B_RENDERER_USE    1    //Uncomment to allow users to select the 16-bit accurate renderer. It has no real purpose (Other than for taking screencaps), since it's slower than the 8-bit renderers.

#endif
