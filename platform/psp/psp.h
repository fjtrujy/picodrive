/*
 * PicoDrive
 * (C) notaz, 2007,2008
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <pspctrl.h>

// vram usage map:
// 000000-044000 fb0: 512*272*2
// 044000-088000 fb1
// 088000-0cc000 depth (Z)
// 0cc000-110000 emu/menu draw buffer 0: 512*272*2
// 110000-154000 emu/menu draw buffer 1

#define VRAMOFFS_FB0    0x00000000
#define VRAMOFFS_FB1    0x00044000
#define VRAMOFFS_DEPTH  0x00088000
#define VRAMOFFS_STUFF  0x000cc000

#define VRAM_FB0        ((void *) (0x44000000+VRAMOFFS_FB0))
#define VRAM_FB1        ((void *) (0x44000000+VRAMOFFS_FB1))
#define VRAM_STUFF      ((void *) (0x44000000+VRAMOFFS_STUFF))

#define VRAM_CACHED_STUFF   ((void *) (0x04000000+VRAMOFFS_STUFF))

#define GU_CMDLIST_SIZE (16*1024)

extern unsigned int guCmdList[GU_CMDLIST_SIZE];

void *psp_video_get_active_fb(void);
extern void *psp_screen;

/* fake 'nub' btns, mapped to the 4 unused upper bits of ctrl buttons */
#define PSP_NUB_UP    (1 << 26)
#define PSP_NUB_RIGHT (1 << 27)
#define PSP_NUB_DOWN  (1 << 28)
#define PSP_NUB_LEFT  (1 << 29)

/* from menu.c */
void psp_menu_init(void);
