// port specific settings

#ifndef PS2_TEXTURES_H
#define PS2_TEXTURES_H

#include <gsKit.h>
#include <gsInline.h>

struct displayMode{
    unsigned char interlace, mode, ffmd;
    unsigned char HsyncsPerMsec;
    unsigned short int width, height;
    unsigned short int VisibleWidth, VisibleHeight;
    unsigned short int StartX, StartY;
};
typedef struct displayMode DISPLAYMODE;

extern GSGLOBAL *gsGlobal;
extern GSTEXTURE *backgroundTexture;
extern DISPLAYMODE *currentDisplayMode;
// extern GSTEXTURE *frameBufferTexture;

void initGSGlobal(void);
void initBackgroundTexture(void);
// void initFrameBufferTexture(void);
void clearGSGlobal(void);

#endif //PS2_TEXTURES_H
