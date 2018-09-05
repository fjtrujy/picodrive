#include "ps2_pico.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include "../../../Pico/pico.h"
#include <pico/pico_int.h>
#include <pico/cd/cue.h>

#include "ps2_config.h"
#include "ps2_drawer.h"
#include "ps2_semaphore.h"
#include "ps2_textures.h"

// TODO: THIS CLASS NEED A REFACTOR
// Extract every use of the texture to the ps2_textures class
// Implement emu_text_out16 and emu_text_out8 functions, so far I don't see here they here
// Make easier to read where the pointers where we are writing the info on the screen 
// Remove the values and constants in the file for something with more sense.

// Private constants

#define OSD_FPS_X 270	//OSD FPS indicator X-coordinate.

//Palette options (Don't change them, since these values might be hardcoded without the use of these definitions). They're listed here to show that they exist.
#define OSD_STAT_BLK_PAL_ENT		0xE0	//OSD black colour palette entry (Used as the background for OSD messages and the CD status LEDs).
#define OSD_TXT_PAL_ENT			0xF0	//OSD text palette entry.
#define OSD_CD_STAT_GREEN_PAL_EN	0xC0	//OSD CD status green LED palette entry
#define OSD_CD_STAT_RED_PAL_EN		0xD0	//OSD CD status red LED palette entry

static unsigned short int FrameBufferTextureVisibleWidth, FrameBufferTextureVisibleHeight;
static unsigned short int FrameBufferTextureVisibleOffsetX, FrameBufferTextureVisibleOffsetY;	//From upper left-hand corner.

// Private Methods

static void emuDrawerFinish(int lagging_behind) {
	lprintf("emuDrawerFinish\n");
	// want vsync?
	if(isVSYNCEnabled() && (!(isVSYNCModeEnabled()) || !lagging_behind)){
		waitSemaphore();
	}
	
	syncGSGlobalChache();
}

static void osd_text(int x, const char *text) {
	lprintf("osd_text\n");
	unsigned short int ScreenHeight;
	int len = strlen(text) * 8;
	char h;

	ScreenHeight=FrameBufferTextureVisibleHeight+FrameBufferTextureVisibleOffsetY;
	if(is8BitsConfig()){
		//8-bit mode
		for (h = 8; h>=0; h--) {
			unsigned char *screen_8 = g_screen_ptr;
			memset(&screen_8[x+FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(ScreenHeight-h-1)], OSD_STAT_BLK_PAL_ENT, len);
		}
		emu_text_out8(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
	else{
		//16-bit mode
		for (h = 8; h >= 0; h--) {
			int pixel_w;
			for(pixel_w=0; pixel_w<len; pixel_w++){
				unsigned short int *screen_16=g_screen_ptr;
				screen_16[x+pixel_w+FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(ScreenHeight-h-1)]=0x8000;
			}
		}
		emu_text_out16(x+FrameBufferTextureVisibleOffsetX, ScreenHeight-8, text);
	}
}

static void do_pal_update(void) {
	lprintf("do_pal_update\n");
	int i;
	unsigned short int *pal=(void *)frameBufferTexture->Clut;
	//Megadrive palette:	0000BBB0GGG0RRR0
	//16-bit clut palette:	ABBBBBGGGGGRRRRR

	if(Pico.video.reg[0xC]&8){
		do_pal_convert_with_shadows(pal, Pico.cram);
	}
	else{
		do_pal_convert(pal, Pico.cram);
		if(rendstatus & PDRAW_SPR_LO_ON_HI) memcpy(&pal[0x80], pal, 0x40*2);
	}
	//For OSD messages and status indicators.
	pal[OSD_STAT_BLK_PAL_ENT] = 0x8000;
	pal[OSD_TXT_PAL_ENT] = 0xFFFF;
	pal[OSD_CD_STAT_GREEN_PAL_EN] = 0x83E0;
	pal[OSD_CD_STAT_RED_PAL_EN] = 0x801F;

  	//Rotate CLUT.
	for (i = 0; i < 256; i++)
	{
		if ((i&0x18) == 8)
		{
			unsigned short int tmp = pal[i];
			pal[i] = pal[i+8];
			pal[i+8] = tmp;
		}
	}

	Pico.m.dirtyPal = 0;
}

static void blitscreen_clut(void) {
	lprintf("blitscreen_clut\n");
	if(Pico.m.dirtyPal){
		do_pal_update();

		SyncDCache(frameBufferTexture->Clut, (void*)((unsigned int)frameBufferTexture->Clut+256*2));
		gsKit_texture_send_inline(gsGlobal, frameBufferTexture->Clut, 16, 16, frameBufferTexture->VramClut, frameBufferTexture->ClutPSM, 1, GS_CLUT_PALLETE);	// upload 16*16 entries (256)
	}
}

static void cd_leds(void) {
	lprintf("cd_leds\n");
    unsigned int reg, col_g, col_r;

	reg = Pico_mcd->s68k_regs[0];

	if (is8BitsConfig()) {
		// 8-bit modes
		col_g = (reg & 2) ? 0xc0c0c0c0 : 0xe0e0e0e0;
		col_r = (reg & 1) ? 0xd0d0d0d0 : 0xe0e0e0e0;
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(2+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(3+FrameBufferTextureVisibleOffsetY)+ 4) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(4+FrameBufferTextureVisibleOffsetY)+ 4) = col_g;
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(2+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(3+FrameBufferTextureVisibleOffsetY)+12) =
		*(unsigned int *)((char *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(4+FrameBufferTextureVisibleOffsetY)+12) = col_r;
	} else {
		// 16-bit modes
		unsigned int *p = (unsigned int *)((short *)g_screen_ptr + FrameBufferTextureVisibleOffsetX+frameBufferTexture->Width*(2+FrameBufferTextureVisibleOffsetY)+4);
		unsigned int col_g = (reg & 2) ? 0x83008300 : 0x80008000;
		unsigned int col_r = (reg & 1) ? 0x80188018 : 0x80008000;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += frameBufferTexture->Width/2 - 12/2;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += frameBufferTexture->Width/2 - 12/2;
		*p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r;
	}
}

static void draw_pico_ptr(void) {
	lprintf("draw_pico_ptr\n");
    unsigned char *p = (unsigned char *)g_screen_ptr;
    
    // only if pen enabled and for 8bit mode
    if (pico_inp_mode == 0 || is16BitsAccurate()) return;
    
    p += 512 * (pico_pen_y + PICO_PEN_ADJUST_Y);
    p += pico_pen_x + PICO_PEN_ADJUST_X;
    p[  -1] = 0xe0; p[   0] = 0xf0; p[   1] = 0xe0;
    p[ 511] = 0xf0; p[ 512] = 0xf0; p[ 513] = 0xf0;
    p[1023] = 0xe0; p[1024] = 0xf0; p[1025] = 0xe0;
}

// Public Methods

void emuDrawerPrepareConfig(void) {
    FrameBufferTextureVisibleOffsetX=0;
    FrameBufferTextureVisibleOffsetY=0;
    FrameBufferTextureVisibleWidth=0;
    FrameBufferTextureVisibleHeight=0;
}

void emuDrawerUpdateConfig(void) {
    if(!isPicoOptAlternativeRenderedEnabled()){	//Accurate (line) renderer.
		FrameBufferTextureVisibleOffsetX=0;	//Nothing to hide here.
		FrameBufferTextureVisibleOffsetY=0;
		FrameBufferTextureVisibleWidth=frameBufferTexture->Width;
		FrameBufferTextureVisibleHeight=frameBufferTexture->Height;
	}
	else{	//8-bit fast (frame) renderer ((320+8)x(224+8+8), as directed by the comment within Draw2.h). Only the draw region will be shown on-screen (320x224 or 320x240).
		FrameBufferTextureVisibleOffsetX=8;

		//Skip borders.
		if(!(Pico.video.reg[1]&8)){	//NTSC.
			FrameBufferTextureVisibleOffsetY=8;	//NTSC has a shorter screen than PAL has.
			FrameBufferTextureVisibleHeight=224;
		}
		else{	//PAL
			FrameBufferTextureVisibleOffsetY=0;
			FrameBufferTextureVisibleHeight=240;
		}

		FrameBufferTextureVisibleWidth=320;

	}
}

void emuDrawerShowInfo(const char *fps, const char *notice, int lagging_behind) {
	lprintf("blit\n");
    int megaCDEnabled = PicoAHW & PAHW_MCD;
    int emulationEnabled = PicoAHW & PAHW_PICO;

	if (notice) {
        osd_text(4, notice);
    }

	if (isShowFPSEnabled()) {
        osd_text(OSD_FPS_X, fps);
    }

	if (isCDLedsEnabled() && megaCDEnabled) {
        cd_leds();
    }
		
	if (emulationEnabled) {
        draw_pico_ptr();
    }
		
	if(is8BitsConfig()) {
        blitscreen_clut();
    }

	SyncDCache(frameBufferTexture->Mem, (void*)((unsigned int)frameBufferTexture->Mem+gsKit_texture_size_ee(frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->PSM)));
	gsKit_texture_send_inline(gsGlobal, frameBufferTexture->Mem, frameBufferTexture->Width, frameBufferTexture->Height, frameBufferTexture->Vram, frameBufferTexture->PSM, frameBufferTexture->TBW, GS_CLUT_TEXTURE); //Use GS_CLUT_TEXTURE for PSM_T8.
	clearFrameBufferTexture();

	emuDrawerFinish(lagging_behind);
}
