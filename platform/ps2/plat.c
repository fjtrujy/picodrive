#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <gsKit.h>
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <libpad.h>
#include <fileXio_rpc.h>
#include <sbv_patches.h>
#include <timer.h>
#include <malloc.h>
#include <audsrv.h>
#include <sys/fcntl.h>
#include <dmaKit.h>
#include <gsInline.h>

#include "plat_ps2.h"

#include "../common/plat.h"
#include "../common/readpng.h"
#include "../common/menu.h"
#include "../common/emu.h"

#include <pico/pico.h>

//Variables, Macros, Enums and Structs

#define DEFAULT_PATH    "mass:"    //Only paths residing on "basic" devices (devices that don't require mounting) can be specified here, since this system doesn't perform mounting based on the path.

#define ANALOG_DEADZONE 0x60

/* fake 'nub' btns */
#define PBTN_NUB_L_UP    0x01000000
#define PBTN_NUB_L_RIGHT 0x02000000
#define PBTN_NUB_L_DOWN  0x04000000
#define PBTN_NUB_L_LEFT  0x08000000
#define PBTN_NUB_R_UP    0x10000000
#define PBTN_NUB_R_RIGHT 0x20000000
#define PBTN_NUB_R_DOWN  0x40000000
#define PBTN_NUB_R_LEFT  0x80000000

GSGLOBAL *gsGlobal = NULL;
static GSTEXTURE BackgroundTexture;
GSTEXTURE FrameBufferTexture;

extern unsigned char POWEROFF_irx_start[];
extern unsigned int POWEROFF_irx_size;

extern unsigned char DEV9_irx_start[];
extern unsigned int DEV9_irx_size;

extern unsigned char ATAD_irx_start[];
extern unsigned int ATAD_irx_size;

extern unsigned char HDD_irx_start[];
extern unsigned int HDD_irx_size;

extern unsigned char PFS_irx_start[];
extern unsigned int PFS_irx_size;

unsigned short int ps2_screen_draw_width, ps2_screen_draw_height, ps2_screen_draw_startX, ps2_screen_draw_startY;

static unsigned char HDDModulesLoaded=0;

static unsigned short int HsyncsPerMsec;
static int VBlankStartSema;

extern unsigned char IOMANX_irx_start[];
extern unsigned int IOMANX_irx_size;

extern unsigned char FILEXIO_irx_start[];
extern unsigned int FILEXIO_irx_size;

extern unsigned char LIBSD_irx_start[];
extern unsigned int LIBSD_irx_size;

extern unsigned char AUDSRV_irx_start[];
extern unsigned int AUDSRV_irx_size;

extern unsigned char USBD_irx_start[];
extern unsigned int USBD_irx_size;

extern unsigned char USBHDFSD_irx_start[];
extern unsigned int USBHDFSD_irx_size;

enum PS2_DISPLAY_MODE{
    PS2_DISPLAY_MODE_AUTO,
    PS2_DISPLAY_MODE_NTSC,
    PS2_DISPLAY_MODE_PAL,
    PS2_DISPLAY_MODE_480P,
    PS2_DISPLAY_MODE_NTSC_NI,
    PS2_DISPLAY_MODE_PAL_NI,
    
    PS2_DISPLAY_MODE_COUNT
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

struct DisplayMode{
    unsigned char interlace, mode, ffmd;
    unsigned char HsyncsPerMsec;
    unsigned short int width, height;
    unsigned short int VisibleWidth, VisibleHeight;
    unsigned short int StartX, StartY;
};

/* PS2 local */
int default_cpu_clock;

struct timeval {
    u32 tv_sec;
    u32 tv_usec;
};

static unsigned char PadArea[2][256] ALIGNED(64);

//Methods

static inline u32 lzw(u32 val)
{
    u32 res;
    __asm__ __volatile__ ("   plzcw   %0, %1    " : "=r" (res) : "r" (val));
    return(res);
}

static int VBlankStartHandler(int cause){
    ee_sema_t sema;
    iReferSemaStatus(VBlankStartSema, &sema);
    if(sema.count<sema.max_count) iSignalSema(VBlankStartSema);
    return 0;
}

static inline void LoadIOPModules(void){
    SifExecModuleBuffer(IOMANX_irx_start, IOMANX_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(FILEXIO_irx_start, FILEXIO_irx_size, 0, NULL, NULL);
    
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    
    SifExecModuleBuffer(USBD_irx_start, USBD_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(USBHDFSD_irx_start, USBHDFSD_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(LIBSD_irx_start, LIBSD_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(AUDSRV_irx_start, AUDSRV_irx_size, 0, NULL, NULL);
}

static void InitializePad(int port, int slot){
    int state;
    
    if((state = padGetState(port, slot))!=PAD_STATE_DISCONN){
        while((state!=PAD_STATE_STABLE) && (state!=PAD_STATE_FINDCTP1)){
            state = padGetState(port, slot);
            
            if(state==PAD_STATE_DISCONN) return;
        }
        
        padSetMainMode(port, slot, PAD_MMODE_DIGITAL, 2);    //It should be PAD_MMODE_UNLOCK, but the homebrew PS2SDK has its value set incorrectly (0 instead of 2).
        
        //Wait for the pad to become ready.
        do{
            state = padGetState(port, slot);
        }while((state!=PAD_STATE_STABLE) && (state!=PAD_STATE_FINDCTP1) && (state!=PAD_STATE_DISCONN));
    }
}

static const char *GetMountParams(const char *command, char *BlockDevice){
    const char *MountPath;
    int BlockDeviceNameLen;
    
    MountPath=NULL;
    if(strlen(command)>6 && (MountPath=strchr(&command[5], ':'))!=NULL){
        BlockDeviceNameLen=(unsigned int)MountPath-(unsigned int)command;
        strncpy(BlockDevice, command, BlockDeviceNameLen);
        BlockDevice[BlockDeviceNameLen]='\0';
        
        MountPath++;    //This is the location of the mount path;
    }
    
    return MountPath;
}

static void SetPWDOnPFS(const char *FullCWD_path){
    int i;
    char *path;
    
    path=NULL;
    for(i=strlen(FullCWD_path); i>=0; i--){ /* Try to seperate the CWD from the path to this ELF. */
        if(FullCWD_path[i]==':'){
            if((path=malloc(i+6+2))!=NULL){
                strcpy(path, "pfs0:/");
                strncat(path, FullCWD_path, i+1);
                path[i+1+6]='\0';
            }
            break;
        }
        else if((FullCWD_path[i]=='\\')||(FullCWD_path[i]=='/')){
            if((path=malloc(i+6+1))!=NULL){
                strcpy(path, "pfs0:/");
                strncat(path, FullCWD_path, i);
                path[i+6]='\0';
            }
            break;
        }
    }
    
    if(path!=NULL){
        chdir(path);
        free(path);
    }
}

u32 ps2_GetTicksInUsec(void){
//    return(clock()/(CLOCKS_PER_SEC*1000000UL));    //Broken.
    return cpu_ticks()/295;
}

static inline unsigned int mSec2HSyncTicks(unsigned int msec){
    return msec*HsyncsPerMsec;
}

static void ThreadWakeupCB(s32 alarm_id, u16 time, void *arg2){
    iWakeupThread(*(int*)arg2);
}

void DelayThread(unsigned short int msec){
    int ThreadID;

    if(msec>0){
        ThreadID=GetThreadId();
        SetAlarm(mSec2HSyncTicks(msec), &ThreadWakeupCB, &ThreadID);
        SleepThread();
    }
}

//HACK! If booting from a USB device, keep trying to open this program again until it succeeds. This will ensure that the emulator will be able to load its files.
static void WaitUntilDeviceIsReady(const char *path){
    FILE *file;

    while((file=fopen(path, "rb"))==NULL){
        //Wait for a while first, or the IOP will get swamped by requests from the EE.
        nopdelay();
        nopdelay();
        nopdelay();
        nopdelay();
        nopdelay();
        nopdelay();
        nopdelay();
        nopdelay();
    };
    fclose(file);
}

int GetBootDeviceID(const char *path){
    int result;

    if(!strncmp(path, "mc0:", 4)) result=BOOT_DEVICE_MC0;
    else if(!strncmp(path, "mc1:", 4)) result=BOOT_DEVICE_MC1;
    else if(!strncmp(path, "cdrom0:", 7)) result=BOOT_DEVICE_CDROM;
    else if(!strncmp(path, "mass:", 5) || !strncmp(path, "mass0:", 6)) result=BOOT_DEVICE_MASS;
    else if(!strncmp(path, "hdd:", 4) || !strncmp(path, "hdd0:", 5)) result=BOOT_DEVICE_HDD;
    else if(!strncmp(path, "host", 4) && ((path[4]>='0' && path[4]<='9') || path[4]==':')) result=BOOT_DEVICE_HOST;
    else result=BOOT_DEVICE_UNKNOWN;

    return result;
}

void ps2_loadHDDModules(void){
    /* Try not to adjust this unless you know what you are doing. The tricky part i keeping the NULL character in the middle of that argument list separated from the number 4. */
    static const char PS2HDD_args[]="-o\0""2";
    static const char PS2FS_args[]="-o\0""8";

    if(!HDDModulesLoaded){
        SifExecModuleBuffer(POWEROFF_irx_start, POWEROFF_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(DEV9_irx_start, DEV9_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(ATAD_irx_start, ATAD_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(HDD_irx_start, HDD_irx_size, sizeof(PS2HDD_args), PS2HDD_args, NULL);
        SifExecModuleBuffer(PFS_irx_start, PFS_irx_size, sizeof(PS2FS_args), PS2FS_args, NULL);
        HDDModulesLoaded=1;
    }
}

void ps2_SetAudioFormat(unsigned int rate){
    struct audsrv_fmt_t AudioFmt;

    AudioFmt.bits = 16;
    AudioFmt.freq = rate;
    AudioFmt.channels = 2;
    audsrv_set_format(&AudioFmt);
}

unsigned int ps2_pad_read(int port, int slot)
{
    struct padButtonStatus buttons;
    unsigned int result;
    int state;
    
    state = padGetState(port, slot);
    if((state==PAD_STATE_STABLE) || (state==PAD_STATE_FINDCTP1)){
        result=(padRead(port, slot, &buttons) != 0)?(0xffff^buttons.btns)&0xFFFF:0;
        
        if(buttons.mode>>4&0xF!=PAD_TYPE_DIGITAL){
            // analog..
            if (buttons.ljoy_h < 128 - ANALOG_DEADZONE) result |= PBTN_NUB_L_LEFT;
            if (buttons.ljoy_h > 128 + ANALOG_DEADZONE) result |= PBTN_NUB_L_RIGHT;
            if (buttons.ljoy_v < 128 - ANALOG_DEADZONE) result |= PBTN_NUB_L_UP;
            if (buttons.ljoy_v > 128 + ANALOG_DEADZONE) result |= PBTN_NUB_L_DOWN;
            
            if (buttons.rjoy_h < 128 - ANALOG_DEADZONE) result |= PBTN_NUB_R_LEFT;
            if (buttons.rjoy_h > 128 + ANALOG_DEADZONE) result |= PBTN_NUB_R_RIGHT;
            if (buttons.rjoy_v < 128 - ANALOG_DEADZONE) result |= PBTN_NUB_R_UP;
            if (buttons.rjoy_v > 128 + ANALOG_DEADZONE) result |= PBTN_NUB_R_DOWN;
        }
    }
    else result=0;
    
    return result;
}

unsigned int ps2_pad_read_all(void){
    return(ps2_pad_read(0, 0)|ps2_pad_read(1, 0));
}

// PLAT METHODS

int plat_get_root_dir(char *dst, int len)
{
    if (len > 0) *dst = 0;
    return 0;
}

int plat_is_dir(const char *path)
{
    if (!fileXioChdir(path)) {
        return 1;
    }
    return 0;
}

void plat_validate_config(void)
{
    
}

void ps2_video_changemode(int bpp)
{
//	ps2_video_changemode_ll(bpp);

  	ps2_memset_all_buffers(0, 0, 320*240*2);
//	ps2_video_flip();
}

void ps2_memcpy_all_buffers(void *data, int offset, int len)
{
    char *dst = (char *)data + offset;
    if (dst != data) memcpy(dst, data, len);
}

void ps2_memset_all_buffers(int offset, int byte, int len)
{
    memset((char *)g_screen_ptr + offset, byte, len);
}

void ps2_make_fb_bufferable(int yes)
{
//	int ret = 0;
//	
//	yes = yes ? 1 : 0;
//	ret |= warm_change_cb_range(WCB_B_BIT, yes, ps2_screens[0], 320*240*2);
//	ret |= warm_change_cb_range(WCB_B_BIT, yes, ps2_screens[1], 320*240*2);
//	ret |= warm_change_cb_range(WCB_B_BIT, yes, ps2_screens[2], 320*240*2);
//	ret |= warm_change_cb_range(WCB_B_BIT, yes, ps2_screens[3], 320*240*2);
//
//	if (ret)
//		fprintf(stderr, "could not make fb buferable.\n");
//	else
//		printf("made fb buferable.\n");
}

/* common */
char cpu_clk_name[16] = "PS2 CPU clocks";

static void menu_prepare_bg(int use_game_bg, int use_fg)
{
    unsigned short int x, y;
    
    for(y=0; y<BackgroundTexture.Height; y++){
        for(x=0; x<BackgroundTexture.Width; x++){
            ((unsigned short int*)BackgroundTexture.Mem)[y*BackgroundTexture.Width+x]=0x8000;	//Clear screen to black.
        }
    }
    
    if (use_game_bg)
    {
        pemu_forced_frame(0);
        
        // darken the active framebuffer
        unsigned short int *dst = (unsigned short int *)BackgroundTexture.Mem;
        int i;
        for (i = 224; i > 0; i--, dst += 320)
        {
            menu_darken_bg(dst, 320, 1);
        }
    }
    else
    {
        // should really only happen once, on startup..
        readpng(BackgroundTexture.Mem, "skin/background.png", READPNG_BG);
    }
}

static void menu_uploadGraphics(void){
    if(FrameBufferTexture.Mem!=NULL) free(FrameBufferTexture.Mem);
    if(FrameBufferTexture.Clut!=NULL){
        free(FrameBufferTexture.Clut);
        FrameBufferTexture.Clut=NULL;
    }
    FrameBufferTexture.Width=320;
    FrameBufferTexture.Height=224;
    FrameBufferTexture.PSM=GS_PSM_CT16;
    FrameBufferTexture.Filter=GS_FILTER_NEAREST;
    FrameBufferTexture.Mem=memalign(128, gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM));
    gsKit_setup_tbw(&FrameBufferTexture);

    g_screen_ptr=(void*)FrameBufferTexture.Mem;
    
    gsKit_vram_clear(gsGlobal);
    
    FrameBufferTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM), GSKIT_ALLOC_USERBUFFER);
    
    BackgroundTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(BackgroundTexture.Width, BackgroundTexture.Height, BackgroundTexture.PSM), GSKIT_ALLOC_USERBUFFER);
    SyncDCache(BackgroundTexture.Mem, (void*)((unsigned int)BackgroundTexture.Mem+gsKit_texture_size_ee(BackgroundTexture.Width, BackgroundTexture.Height, BackgroundTexture.PSM)));
    gsKit_texture_send_inline(gsGlobal, BackgroundTexture.Mem, BackgroundTexture.Width, BackgroundTexture.Height, BackgroundTexture.Vram, BackgroundTexture.PSM, BackgroundTexture.TBW, GS_CLUT_NONE);
}

void ps2_SetDisplayMode(int mode){
    struct DisplayMode modes[PS2_DISPLAY_MODE_COUNT]={
        {GS_INTERLACED, 0, GS_FIELD, 16, 640, 448, 640, 448, 0, 0},
        {GS_INTERLACED, GS_MODE_NTSC, GS_FIELD, 16, 640, 448, 640, 448, 0, 0},        //HSYNCs per millisecond: 15734Hz/1000=15.734
        {GS_INTERLACED, GS_MODE_PAL, GS_FIELD, 16, 640, 512, 640, 480, 0, 16},        //HSYNCs per millisecond: 15625Hz/1000=15.625
        {GS_NONINTERLACED, GS_MODE_DTV_480P, GS_FRAME, 31, 720, 480, 640, 448, 40, 16},    //HSYNCs per millisecond: 31469Hz/1000=31.469
        {GS_NONINTERLACED, GS_MODE_NTSC, GS_FIELD, 16, 640, 224, 640, 224, 0, 0},    //HSYNCs per millisecond: 15734Hz/1000=15.734
        {GS_NONINTERLACED, GS_MODE_PAL, GS_FIELD, 16, 640, 256, 640, 240, 0, 16},    //HSYNCs per millisecond: 15625Hz/1000=15.625
    };
    
    /* Initilize the GS */
    if(gsGlobal!=NULL) gsKit_deinit_global(gsGlobal);
    gsGlobal=gsKit_init_global();
    
    gsGlobal->DoubleBuffering = GS_SETTING_OFF;    /* Disable double buffering to get rid of the "Out of VRAM" error */
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;    /* Enable alpha blending for primitives. */
    gsGlobal->ZBuffering = GS_SETTING_OFF;
    gsGlobal->PSM=GS_PSM_CT16;
    
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
    
    ps2_screen_draw_width=modes[mode].VisibleWidth;
    ps2_screen_draw_height=modes[mode].VisibleHeight;
    HsyncsPerMsec=modes[mode].HsyncsPerMsec;
    ps2_screen_draw_startX=modes[mode].StartX;
    ps2_screen_draw_startY=modes[mode].StartY;
    
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

static inline void InitGS(void){
    /* Initilize DMAKit */
    dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    
    /* Initialize the DMAC */
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    
    ps2_SetDisplayMode(PS2_DISPLAY_MODE_AUTO);
}

void plat_video_menu_enter(int is_rom_loaded)
{
    BackgroundTexture.Width=320;
    BackgroundTexture.Height=240;
    BackgroundTexture.PSM=GS_PSM_CT16;
    BackgroundTexture.Delayed=GS_SETTING_ON;
    BackgroundTexture.Filter=GS_FILTER_NEAREST;
    BackgroundTexture.Mem=memalign(128, gsKit_texture_size_ee(BackgroundTexture.Width, BackgroundTexture.Height, BackgroundTexture.PSM));
    gsKit_setup_tbw(&BackgroundTexture);

    menu_prepare_bg(is_rom_loaded, 1);
    
    menu_uploadGraphics();
}

// clears whole screen.
void ps2_ClearScreen(void)
{
    memset(FrameBufferTexture.Mem, 0, gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM));
}

void plat_video_menu_begin(void)
{
    ps2_ClearScreen();
    gsKit_clear(gsGlobal, GS_BLACK);
    gsKit_prim_sprite_texture(gsGlobal, &BackgroundTexture, ps2_screen_draw_startX, ps2_screen_draw_startY, 0, 0, ps2_screen_draw_startX+ps2_screen_draw_width, ps2_screen_draw_startY+ps2_screen_draw_height, BackgroundTexture.Width, BackgroundTexture.Height, 0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
    
    plat_video_menu_end();
}

void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2)
{
    //This results in scaling errors:
/*    gsKit_prim_sprite_texture(gsGlobal, &FrameBufferTexture, ps2_screen_draw_startX, ps2_screen_draw_startY,
                                                        u1, v1,
                                                        ps2_screen_draw_startX+ps2_screen_draw_width, ps2_screen_draw_startY+ps2_screen_draw_height,
                                                        u2, v2,
                                                        1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00)); */

    //This is a custom drawing function, since gsKit_prim_sprite_texture_3d performs a series of weird calculations that result in the frame buffer being stretched a little. I think that those calculations were meant to achieve "pixel-perfect" sprite drawing, as demonstrated by jbit... but that doesn't seem to work well with nearest filtering. D:
    gsKit_set_texfilter(gsGlobal, FrameBufferTexture.Filter);

    u64* p_store;
    u64* p_data;
    int qsize = 4;
    int bsize = 64;

    int ix1 = (int)(ps2_screen_draw_startX * 16.0f) + gsGlobal->OffsetX;
    int ix2 = (int)((ps2_screen_draw_startX+ps2_screen_draw_width) * 16.0f) + gsGlobal->OffsetX;
    int iy1 = (int)(ps2_screen_draw_startY * 16.0f) + gsGlobal->OffsetY;
    int iy2 = (int)((ps2_screen_draw_startY+ps2_screen_draw_height) * 16.0f) + gsGlobal->OffsetY;

    int iu1 = (int)(u1 * 16.0f);
    int iu2 = (int)(u2 * 16.0f);
    int iv1 = (int)(v1 * 16.0f);
    int iv2 = (int)(v2 * 16.0f);


    int tw = 31 - (lzw(FrameBufferTexture.Width) + 1);
    if(FrameBufferTexture.Width > (1<<tw))
        tw++;

    int th = 31 - (lzw(FrameBufferTexture.Height) + 1);
    if(FrameBufferTexture.Height > (1<<th))
        th++;

    p_store = p_data = gsKit_heap_alloc(gsGlobal, qsize, bsize, GIF_PRIM_SPRITE_TEXTURED);

    *p_data++ = GIF_TAG_SPRITE_TEXTURED(0);
    *p_data++ = GIF_TAG_SPRITE_TEXTURED_REGS(gsGlobal->PrimContext);

    if(FrameBufferTexture.VramClut == 0)
    {
        *p_data++ = GS_SETREG_TEX0(FrameBufferTexture.Vram/256, FrameBufferTexture.TBW, FrameBufferTexture.PSM,
            tw, th, gsGlobal->PrimAlphaEnable, 0,
            0, 0, 0, 0, GS_CLUT_STOREMODE_NOLOAD);
    }
    else
    {
        *p_data++ = GS_SETREG_TEX0(FrameBufferTexture.Vram/256, FrameBufferTexture.TBW, FrameBufferTexture.PSM,
            tw, th, gsGlobal->PrimAlphaEnable, 0,
            FrameBufferTexture.VramClut/256, FrameBufferTexture.ClutPSM, 0, 0, GS_CLUT_STOREMODE_LOAD);
    }

    *p_data++ = GS_SETREG_PRIM( GS_PRIM_PRIM_SPRITE, 0, 1, gsGlobal->PrimFogEnable,
                gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable,
                1, gsGlobal->PrimContext, 0);

    *p_data++ = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);

    *p_data++ = GS_SETREG_UV( iu1, iv1 );
    *p_data++ = GS_SETREG_XYZ2( ix1, iy1, 1 );

    *p_data++ = GS_SETREG_UV( iu2, iv2 );
    *p_data++ = GS_SETREG_XYZ2( ix2, iy2, 1 );
}

static void ps2_redrawFrameBufferTexture(void){
    SyncDCache(FrameBufferTexture.Mem, (void*)((unsigned int)FrameBufferTexture.Mem+gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM)));
    gsKit_texture_send_inline(gsGlobal, FrameBufferTexture.Mem, FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.Vram, FrameBufferTexture.PSM, FrameBufferTexture.TBW, GS_CLUT_NONE);
    ps2_DrawFrameBuffer(0, 0, FrameBufferTexture.Width, FrameBufferTexture.Height);
}

void FlipFBNoSync(void){
    //gsKit_switch_context(gsGlobal);    //Occasionally causes fast frame draws to lose some graphics. Since double buffering isn't used, perhaps that's caused by the 2nd frame buffer being invalid? In that case, not calling this function is the solution, I suppose.
    gsKit_queue_exec(gsGlobal);
}

void SyncFlipFB(void){
    PollSema(VBlankStartSema);    //Clear the semaphore to zero if it isn't already at zero, so that WaitSema will wait until the next VBlank start event.
    WaitSema(VBlankStartSema);
    FlipFBNoSync();
}

void plat_video_menu_end(void)
{
    ps2_redrawFrameBufferTexture();
    SyncFlipFB();
    /*
     FIXME: I don't know whether it's really a bug or not, but draw_menu_video_mode() fails to display text. I believe that it's because the GS is taking a while to receive/draw the uploaded frame buffer, and so the text stays on-screen for barely any time at all.
     Some things that makes that problem vanish:
     1. Adding a call to SyncFlipFB() at the end of ps2_redrawFrameBufferTexture().
     2. Invoking dmaKit_wait_fast below (Presumably forces the syste, to wait for the frame buffer to be transferred to VRAM).
     3. Instead of polling the pads directly, use the wait_for_input() function. It'll slow things down... but the text will somehow appear. This is how I arrived at my hypothesis that the menu frame buffer is spending nearly no time on-screen at all (When compared to the background).
     */
    dmaKit_wait_fast();
}

void plat_video_menu_deinit(void)
{
    if(BackgroundTexture.Mem!=NULL){
        free(BackgroundTexture.Mem);
        BackgroundTexture.Mem=NULL;
    }
    if(FrameBufferTexture.Mem!=NULL){
        free(FrameBufferTexture.Mem);
        FrameBufferTexture.Mem=NULL;
    }
    if(FrameBufferTexture.Clut!=NULL){
        free(FrameBufferTexture.Clut);
        FrameBufferTexture.Clut=NULL;
    }
}

void plat_early_init(void)
{
    ee_sema_t sema;
    char cwd[FILENAME_MAX], BlockDevice[16];
    const char *MountPoint;
    int BootDeviceID;

    SifInitRpc(0);
//    while(!SifIopReset(NULL, 0)){};

    getcwd(cwd, sizeof(cwd));
    BootDeviceID=GetBootDeviceID(cwd);

    ChangeThreadPriority(GetThreadId(), MAIN_THREAD_PRIORITY);

    sema.init_count=0;
    sema.max_count=1;
    sema.attr=sema.option=0;
    VBlankStartSema=CreateSema(&sema);

    AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
    EnableIntc(kINTC_VBLANK_START);

    while(!SifIopSync()){};

    SifInitRpc(0);

    sbv_patch_enable_lmb();

    LoadIOPModules();

    fileXioInit();
    audsrv_init();
    ps2_SetAudioFormat(22050);
    audsrv_set_volume(MAX_VOLUME);

    padInit(0);
    padPortOpen(0, 0, PadArea[0]);
    padPortOpen(1, 0, PadArea[1]);
    InitializePad(0, 0);
    InitializePad(1, 0);

    InitGS();
    memset(&FrameBufferTexture, 0, sizeof(FrameBufferTexture));

    //Mount the HDD partition, if required.
    if(BootDeviceID==BOOT_DEVICE_HDD){
        ps2_loadHDDModules();

        //Attempt to mount the partition.
        if((MountPoint=GetMountParams(cwd, BlockDevice))!=NULL && !strncmp(MountPoint, "pfs:", 4)){
            fileXioMount("pfs0:", BlockDevice, FIO_MT_RDWR);

            SetPWDOnPFS(&MountPoint[4]);
        }
    }
    else if(BootDeviceID==BOOT_DEVICE_CDROM || BootDeviceID==BOOT_DEVICE_UNKNOWN){
        chdir(DEFAULT_PATH);
    }
    else if(BootDeviceID==BOOT_DEVICE_MASS){
//        WaitUntilDeviceIsReady(argv[0]);
    }
}

void plat_init(void)
{
}

void plat_finish(void)
{
    if(FrameBufferTexture.Mem!=NULL) free(FrameBufferTexture.Mem);
    if(FrameBufferTexture.Clut!=NULL) free(FrameBufferTexture.Clut);

    gsKit_deinit_global(gsGlobal);

    DisableIntc(kINTC_VBLANK_START);
    RemoveIntcHandler(kINTC_VBLANK_START, 0);
    DeleteSema(VBlankStartSema);

    padPortClose(0, 0);
    padPortClose(1, 0);
    padEnd();

    fileXioUmount("pfs0:");

    fileXioExit();
    SifExitRpc();
}

void lprintf(const char *fmt, ...)
{
	va_list vl;

	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
}

int plat_wait_event(int *fds_hnds, int count, int timeout_ms)
{
    struct timeval tv, *timeout = NULL;
    int i, ret, fdmax = -1;
//    fd_set fdset;
    
    return ret;
}

void plat_sleep_ms(int ms)
{
    DelayThread(ms);
}

//stdio.h has this defined, but it's missing.
int setvbuf ( FILE * stream, char * buffer, int mode, size_t size ){
    return -1;
}
