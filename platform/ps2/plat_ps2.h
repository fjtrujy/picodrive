#ifndef __PS2_H__
#define __PS2_H__

#include <gsKit.h>

#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00) // turn black GS Screen
#define GS_GREY GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00) // turn grey GS Screen

//Thread priorities (lower = higher priority)
#define MAIN_THREAD_PRIORITY    0x51
#define SOUND_THREAD_PRIORITY    0x50

struct displayMode{
    unsigned char interlace, mode, ffmd;
    unsigned char HsyncsPerMsec;
    unsigned short int width, height;
    unsigned short int VisibleWidth, VisibleHeight;
    unsigned short int StartX, StartY;
};
typedef struct displayMode DISPLAYMODE;

enum PS2_DISPLAY_MODE{
    PS2_DISPLAY_MODE_AUTO,
    PS2_DISPLAY_MODE_NTSC,
    PS2_DISPLAY_MODE_PAL,
    PS2_DISPLAY_MODE_480P,
    PS2_DISPLAY_MODE_NTSC_NI,
    PS2_DISPLAY_MODE_PAL_NI,
    
    PS2_DISPLAY_MODE_COUNT
};

enum PS2_TEXTURES_Z_POSITION {
    PS2_TEXTURES_Z_POSITION_BACKGROUND = 0,
    PS2_TEXTURES_Z_POSITION_BUFFER,

    PS2_TEXTURES_Z_POSITION_COUNT
};

enum BootDeviceIDs{
    BOOT_DEVICE_UNKNOWN = -1,
    BOOT_DEVICE_MC0 = 0,
    BOOT_DEVICE_MC1,
    BOOT_DEVICE_CDROM,
    BOOT_DEVICE_MASS,
    BOOT_DEVICE_HDD,
    BOOT_DEVICE_HOST,
    
    BOOT_DEVICE_COUNT,
};

/* video */
void ps2_ClearScreen(void);

/* emu */
void DelayThread(unsigned short int msec);
void FlipFBNoSync(void);
void SyncFlipFB(void);

#endif
