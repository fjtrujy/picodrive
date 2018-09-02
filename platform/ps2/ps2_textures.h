// port specific settings

#ifndef PS2_TEXTURES_H
#define PS2_TEXTURES_H

#include <gsKit.h>
#include <gsInline.h>

#include "plat_ps2.h"

extern GSGLOBAL *gsGlobal;
extern DISPLAYMODE *currentDisplayMode;
extern GSTEXTURE *frameBufferTexture;

void initGSGlobal(void);
void initBackgroundTexture(void);
void initFrameBufferTexture(void);

void clearGSGlobal(void);
void clearBackgroundTexture(void);
void clearFrameBufferTexture(void);

void syncGSGlobalChache(void);
void syncBackgroundChache(void);
void syncFrameBufferChache(void);

void resetFrameBufferTexture(void);

void deinitGSGlobal(void);
void deinitFrameBufferTexture(void);

#endif //PS2_TEXTURES_H
