// port specific settings

#ifndef PS2_TEXTURES_H
#define PS2_TEXTURES_H

#include <gsKit.h>
#include <gsInline.h>

#include "plat_ps2.h"

extern GSGLOBAL *gsGlobal;
extern GSTEXTURE *backgroundTexture;
extern DISPLAYMODE *currentDisplayMode;
// extern GSTEXTURE *frameBufferTexture;

void initGSGlobal(void);
void initBackgroundTexture(void);
// void initFrameBufferTexture(void);

void clearGSGlobal(void);
void clearBackgroundTexture(void);

void syncBackgroundChache(void);

#endif //PS2_TEXTURES_H
