#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <fileXio_rpc.h>
#include <audsrv.h>
#include <gsKit.h>
#include <gsInline.h>

#include "plat_ps2.h"
#include "version.h"

#include "../common/plat.h"
#include "../common/menu.h"
#include "../common/emu.h"

//Variables, Macros, Enums and Structs

#define DEFAULT_PATH    "mass:"    //Only paths residing on "basic" devices (devices that don't require mounting) can be specified here, since this system doesn't perform mounting based on the path.

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

static unsigned char HDDModulesLoaded=0;

unsigned short int ps2_screen_draw_width, ps2_screen_draw_height, ps2_screen_draw_startX, ps2_screen_draw_startY;

static unsigned short int HsyncsPerMsec;
static int VBlankStartSema;

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

void ps2_texture_deinit(void *texture_ptr)
{
    if(texture_ptr!=NULL){
        free(texture_ptr);
        texture_ptr=NULL;
    }
}

void  prepare_texture(GSTEXTURE *texture, int delayed)
{
    texture->Width=SCREEN_WIDTH;
    texture->Height=SCREEN_HEIGHT;
    texture->PSM=GS_PSM_CT16;
    if (delayed) {
        texture->Delayed=GS_SETTING_ON;
    }
    texture->Filter=GS_FILTER_NEAREST;
    texture->Mem=memalign(128, gsKit_texture_size_ee(texture->Width, texture->Height, texture->PSM));
    gsKit_setup_tbw(texture);
}

/* common */
char cpu_clk_name[16] = "PS2 CPU clocks";

void ps2_ClearFrameBuffer(void){
    ps2_texture_deinit(FrameBufferTexture.Mem);
    ps2_texture_deinit(FrameBufferTexture.Clut);
    
    gsKit_vram_clear(gsGlobal);
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

// clears whole screen.
void ps2_ClearScreen(void)
{
    memset(g_screen_ptr, 0, gsKit_texture_size_ee(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM));
}

void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2)
{
    gsKit_prim_sprite_texture(gsGlobal, &FrameBufferTexture, ps2_screen_draw_startX, ps2_screen_draw_startY,
                                                        u1, v1,
                                                        ps2_screen_draw_startX+ps2_screen_draw_width, ps2_screen_draw_startY+ps2_screen_draw_height,
                                                        u2, v2,
                                                        1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

void ps2_SyncTextureChache(GSTEXTURE *texture)
{
    SyncDCache(texture->Mem, (void*)((unsigned int)texture->Mem+gsKit_texture_size_ee(texture->Width, texture->Height, texture->PSM)));
    gsKit_texture_send_inline(gsGlobal, texture->Mem, texture->Width, texture->Height, texture->Vram, texture->PSM, texture->TBW, GS_CLUT_NONE);
}

static void ps2_redrawFrameBufferTexture(void){
    ps2_SyncTextureChache(&FrameBufferTexture);
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

//Additional functions

void lprintf(const char *fmt, ...)
{
    va_list vl;
    
    va_start(vl, fmt);
    vprintf(fmt, vl);
    va_end(vl);
}

//stdio.h has this defined, but it's missing.
int setvbuf ( FILE * stream, char * buffer, int mode, size_t size ){
    return -1;
}

// PLAT METHODS

int plat_get_root_dir(char *dst, int len)
{
    if (len > 0) *dst = 0;
    return 0;
}

int plat_is_dir(const char *path)
{
    iox_stat_t cpstat;
    int isDir = 0;
    int iret;
    // is this a dir or a full path?
    iret = fileXioGetStat(path, &cpstat);
    if (iret >= 0 && FIO_S_ISDIR(cpstat.mode)){
        isDir = 1;
    }
    
    return isDir;
}

void *plat_mmap(unsigned long addr, size_t size)
{
    void *req, *ret;
    //    TODO FJTRUJY import mmap function
    //    req = (void *)addr;
    //    ret = mmap(req, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    //    if (ret == MAP_FAILED)
    //        return NULL;
    //    if (ret != req)
    //        printf("warning: mmaped to %p, requested %p\n", ret, req);
    ret = calloc(1, size);
    
    return ret;
}

void plat_munmap(void *ptr, size_t size)
{
    //    TODO FJTRUJY import munmap function
    //    munmap(ptr, size);
    free(ptr);
}

void plat_video_menu_enter(int is_rom_loaded)
{
    ps2_ClearFrameBuffer();
    
    prepare_texture(&FrameBufferTexture, 0);
    g_screen_ptr=(void*)FrameBufferTexture.Mem;
    
    FrameBufferTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(FrameBufferTexture.Width, FrameBufferTexture.Height, FrameBufferTexture.PSM), GSKIT_ALLOC_USERBUFFER);
    
    BackgroundTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(BackgroundTexture.Width, BackgroundTexture.Height, BackgroundTexture.PSM), GSKIT_ALLOC_USERBUFFER);
    
    ps2_SyncTextureChache(&BackgroundTexture);
}

void plat_video_menu_begin(void)
{
    ps2_ClearScreen();
    gsKit_clear(gsGlobal, GS_BLACK);
    gsKit_prim_sprite_texture(gsGlobal, &BackgroundTexture, ps2_screen_draw_startX, ps2_screen_draw_startY, 0, 0, ps2_screen_draw_startX+ps2_screen_draw_width, ps2_screen_draw_startY+ps2_screen_draw_height, BackgroundTexture.Width, BackgroundTexture.Height, 0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
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

void plat_early_init(void)
{
    ee_sema_t sema;

    SifInitRpc(0);
//    while(!SifIopReset(NULL, 0)){}; // Comment this line if you don't wanna debug the output

    ChangeThreadPriority(GetThreadId(), MAIN_THREAD_PRIORITY);

    sema.init_count=0;
    sema.max_count=1;
    sema.attr=sema.option=0;
    VBlankStartSema=CreateSema(&sema);

    AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
    EnableIntc(kINTC_VBLANK_START);

    while(!SifIopSync()){};
}

void plat_init(void)
{
    char cwd[FILENAME_MAX], BlockDevice[16];
    const char *MountPoint;
    int BootDeviceID;
    
    getcwd(cwd, sizeof(cwd));
    BootDeviceID=GetBootDeviceID(cwd);
    
    SifInitRpc(0);
    
    sbv_patch_enable_lmb();
    
    LoadIOPModules();
    
    fileXioInit();
    audsrv_init();
    
    InitGS();
    memset(&FrameBufferTexture, 0, sizeof(FrameBufferTexture));
    
    prepare_texture(&BackgroundTexture, 1);
    g_menubg_ptr = BackgroundTexture.Mem;
    
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

void plat_finish(void)
{
    ps2_texture_deinit(FrameBufferTexture.Mem);
    ps2_texture_deinit(FrameBufferTexture.Clut);

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

int plat_wait_event(int *fds_hnds, int count, int timeout_ms)
{
    return -1;
}

void plat_sleep_ms(int ms)
{
    DelayThread(ms);
}

unsigned int plat_get_ticks_ms(void)
{
    return plat_get_ticks_us()/1000;
}

unsigned int plat_get_ticks_us(void)
{
    //    return(clock()/(CLOCKS_PER_SEC*1000000UL));    //Broken.
    return cpu_ticks()/295;
}

void plat_wait_till_us(unsigned int us_to)
{
    unsigned int now, diff;
    diff = (us_to-plat_get_ticks_us())/1000;
    
    if (diff > 0 && diff < 50 ) { // This maximum is to avoid the restart cycle of the PS2 cpu_ticks
        DelayThread(diff);
    }
}

void plat_video_flip(void)
{
}

void plat_video_wait_vsync(void)
{
}

void plat_status_msg_clear(void)
{
}

void plat_status_msg_busy_next(const char *msg)
{
    plat_status_msg_clear();
    pemu_finalize_frame("", msg);
    emu_status_msg("");
    
    /* assumption: msg_busy_next gets called only when
     * something slow is about to happen */
    reset_timing = 1;
}

void plat_status_msg_busy_first(const char *msg)
{
    ps2_memcpy_all_buffers(g_screen_ptr, 0, SCREEN_WIDTH*SCREEN_HEIGHT*2);
    plat_status_msg_busy_next(msg);
}

void plat_update_volume(int has_changed, int is_up)
{
}

void plat_debug_cat(char *str)
{
    strcat(str, (currentConfig.EmuOpt&0x80) ? "soft clut\n" : "hard clut\n");    //TODO: is this valid for this port?
}

const char *plat_get_credits(void)
{
    return "PicoDrive v" VERSION " (c) notaz, 06-09\n\n"
    "Returned life by fjtrujy (thanks sp193)\n/n"
    "Credits:\n"
    "fDave: Cyclone 68000 core,\n"
    "      base code of PicoDrive\n"
    "Reesy & FluBBa: DrZ80 core\n"
    "MAME devs: YM2612 and SN76496 cores\n"
    "rlyeh and others: minimal SDK\n"
    "Squidge: mmuhack\n"
    "Dzz: ARM940 sample\n"
    "\n"
    "Special thanks (for docs, ideas):\n"
    " Charles MacDonald, Haze,\n"
    " Stephane Dallongeville,\n"
    " Lordus, Exophase, Rokas,\n"
    " Nemesis, Tasco Deluxe";
}
