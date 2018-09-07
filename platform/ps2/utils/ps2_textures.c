#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include <pico/pico_int.h>

#include "../../common/emu.h"
#include "../../common/menu.h"
#include "../port_config.h"

#include "ps2_textures.h"

// PRIVATE VARIABLES

// PUBLIC VARIABLES

GSGLOBAL *gsGlobal = NULL;
GSTEXTURE *backgroundTexture = NULL;
DISPLAYMODE *currentDisplayMode = NULL;
GSTEXTURE *frameBufferTexture = NULL;

// PRIVATE METHODS

static u32 textureSize(GSTEXTURE *texture) {
    return gsKit_texture_size(texture->Width, texture->Height, texture->PSM);
}

static void *textureEndPTR(GSTEXTURE *texture) {
    return ((void *)((unsigned int)texture->Mem+textureSize(texture)));
}

static size_t gskitTextureSize(GSTEXTURE *texture) {
    return gsKit_texture_size_ee(texture->Width, texture->Height, texture->PSM);
}

static void prepareTexture(GSTEXTURE *texture, int delayed) {
    texture->Width=SCREEN_WIDTH;
    texture->Height=SCREEN_HEIGHT;
    texture->PSM=GS_PSM_CT16;
    if (delayed) {
        texture->Delayed=GS_SETTING_ON;
    }
    texture->Filter=GS_FILTER_NEAREST;
    texture->Mem=memalign(128, gskitTextureSize(texture));
    gsKit_setup_tbw(texture);
}

static void setDisplayMode(int mode) {
    struct displayMode modes[PS2_DISPLAY_MODE_COUNT]={
        {GS_INTERLACED, 0, GS_FIELD, 16, 640, 448, 640, 448, 0, 0},
        {GS_INTERLACED, GS_MODE_NTSC, GS_FIELD, 16, 640, 448, 640, 448, 0, 0},        //HSYNCs per millisecond: 15734Hz/1000=15.734
        {GS_INTERLACED, GS_MODE_PAL, GS_FIELD, 16, 640, 512, 640, 480, 0, 16},        //HSYNCs per millisecond: 15625Hz/1000=15.625
        {GS_NONINTERLACED, GS_MODE_DTV_480P, GS_FRAME, 31, 720, 480, 640, 448, 40, 16},    //HSYNCs per millisecond: 31469Hz/1000=31.469
        {GS_NONINTERLACED, GS_MODE_NTSC, GS_FIELD, 16, 640, 224, 640, 224, 0, 0},    //HSYNCs per millisecond: 15734Hz/1000=15.734
        {GS_NONINTERLACED, GS_MODE_PAL, GS_FIELD, 16, 640, 256, 640, 240, 0, 16},    //HSYNCs per millisecond: 15625Hz/1000=15.625
    };
    
    if(mode!=PS2_DISPLAY_MODE_AUTO){
        gsGlobal->Interlace=modes[mode].interlace;
        gsGlobal->Mode=modes[mode].mode;
        gsGlobal->Field=modes[mode].ffmd;
        gsGlobal->Width=modes[mode].width;
        gsGlobal->Height=modes[mode].height;
    }
    else{
        mode=gsGlobal->Mode==GS_MODE_PAL?PS2_DISPLAY_MODE_PAL:PS2_DISPLAY_MODE_NTSC;
    }
    
    currentDisplayMode = malloc(sizeof *currentDisplayMode);
    currentDisplayMode->VisibleWidth = modes[mode].VisibleWidth;
    currentDisplayMode->VisibleHeight = modes[mode].VisibleHeight;
    currentDisplayMode->HsyncsPerMsec = modes[mode].HsyncsPerMsec;
    currentDisplayMode->StartX = modes[mode].StartX;
    currentDisplayMode->StartY = modes[mode].StartY;
    
    gsKit_init_screen(gsGlobal);    /* Apply settings. */
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    
    //gsKit doesn't set the TEXA register for expanding the alpha value of 16-bit textures, so we have to set it up here.
    u64 *p_data;
    p_data = gsKit_heap_alloc(gsGlobal, 1 ,16, GIF_AD);
    *p_data++ = GIF_TAG_AD(1);
    *p_data++ = GIF_AD;
    *p_data++ = GS_SETREG_TEXA(0x80, 0, 0x00);    // When alpha = 0, use 0x80. If 1, use 0x00.
    *p_data++ = GS_TEXA;
}

static void syncTextureChache(GSTEXTURE *texture) {
    SyncDCache(texture->Mem, textureEndPTR(texture));
    gsKit_texture_send_inline(gsGlobal, texture->Mem, texture->Width, texture->Height, texture->Vram, texture->PSM, texture->TBW, GS_CLUT_NONE);
}

static void deinitTexturePTR(void *texture_ptr) {
    if(texture_ptr!=NULL){
        free(texture_ptr);
        texture_ptr=NULL;
    }
}

static void deinitTexture(GSTEXTURE *texture) {
    texture->Width=0;
    texture->Height=0;
    deinitTexturePTR(texture->Mem);
    deinitTexturePTR(texture->Clut);
}

// PUBLIC METHODS

void initGSGlobal(void) {
    /* Initilize DMAKit */
    dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    
    /* Initialize the DMAC */
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    // /* Initilize the GS */
    if(gsGlobal!=NULL) {
        gsKit_deinit_global(gsGlobal);
    } 
    gsGlobal=gsKit_init_global();

    gsGlobal->DoubleBuffering = GS_SETTING_OFF;    /* Disable double buffering to get rid of the "Out of VRAM" error */
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;    /* Enable alpha blending for primitives. */
    gsGlobal->ZBuffering = GS_SETTING_OFF;
    gsGlobal->PSM=GS_PSM_CT16;

    setDisplayMode(PS2_DISPLAY_MODE_AUTO);
}

void initBackgroundTexture(void) {
    backgroundTexture = malloc(sizeof *backgroundTexture);
    prepareTexture(backgroundTexture, 1);

    g_menubg_ptr = backgroundTexture->Mem; // this pointer is used in the common classes
}

void initFrameBufferTexture(void) {
    frameBufferTexture = malloc(sizeof *frameBufferTexture);
    prepareTexture(frameBufferTexture, 0);

    g_screen_ptr = frameBufferTexture->Mem; // this pointer is used in the common classes
    PicoDraw2FB = g_screen_ptr; // this pointer is used in the Pico classes
}

void clearGSGlobal(void) {
    gsKit_clear(gsGlobal, GS_BLACK);
    gsKit_vram_clear(gsGlobal);
}

void clearBackgroundTexture(void) {
    gsKit_prim_sprite_texture(gsGlobal, backgroundTexture, currentDisplayMode->StartX, currentDisplayMode->StartY, 0, 0, currentDisplayMode->StartX+currentDisplayMode->VisibleWidth, currentDisplayMode->StartY+currentDisplayMode->VisibleHeight, backgroundTexture->Width, backgroundTexture->Height, PS2_TEXTURES_Z_POSITION_BACKGROUND, GS_GREY);
}

void clearFrameBufferTexture(void) {
    gsKit_prim_sprite_texture(gsGlobal, frameBufferTexture, currentDisplayMode->StartX, currentDisplayMode->StartY, 0, 0, currentDisplayMode->StartX+currentDisplayMode->VisibleWidth, currentDisplayMode->StartY+currentDisplayMode->VisibleHeight, frameBufferTexture->Width, frameBufferTexture->Height, PS2_TEXTURES_Z_POSITION_BUFFER, GS_GREY);
}

void syncGSGlobalChache(void) {
     gsKit_queue_exec(gsGlobal);
}

void syncBackgroundChache(void) {
    // We need to create the VRAM just in this state I dont know why...
    backgroundTexture->Vram=gsKit_vram_alloc(gsGlobal, textureSize(backgroundTexture) , GSKIT_ALLOC_USERBUFFER);
    syncTextureChache(backgroundTexture);
}

void syncFrameBufferChache(void) {
    // We need to create the VRAM just in this state I dont know why...
    frameBufferTexture->Vram=gsKit_vram_alloc(gsGlobal, textureSize(frameBufferTexture) , GSKIT_ALLOC_USERBUFFER);
    syncTextureChache(frameBufferTexture);
}

void resetFrameBufferTexture(void) {
    memset(frameBufferTexture->Mem, 0, gskitTextureSize(frameBufferTexture));
}

void deinitGSGlobal(void) {
    gsKit_deinit_global(gsGlobal);
}

void deinitBackgroundTexture(void) {
    deinitTexture(backgroundTexture);
}

void deinitFrameBufferTexture(void) {
    deinitTexture(frameBufferTexture);
    g_screen_ptr = NULL; // this pointer is used in the common classes
    PicoDraw2FB = NULL; // this pointer is used in the Pico classes
}

void deinitPS2Textures(void) {
    deinitFrameBufferTexture();
    deinitBackgroundTexture();
    deinitGSGlobal();
}


// gsKit_setup_tbw(frameBufferTexture);
// 	frameBufferTexture->Mem=memalign(128, gsKit_texture_size_ee(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM));
// 	frameBufferTexture->Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM), GSKIT_ALLOC_USERBUFFER);
// 	DrawLineDest=PicoDraw2FB=g_screen_ptr=(void*)((unsigned int)frameBufferTexture->Mem);

// 	resetFrameBufferTexture();