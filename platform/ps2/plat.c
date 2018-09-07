#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <fileXio_rpc.h>
#include <audsrv.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <unistd.h>
#include <libpad.h>

#include "utils/io_suppliment.h"
#include "utils/ps2_config.h"
#include "utils/ps2_modules.h"
#include "utils/ps2_semaphore.h"
#include "utils/ps2_textures.h"
#include "utils/ps2_timing.h"
#include "version.h"

#include "../common/emu.h"
#include "../common/plat.h"

//Methods
//This method should be called from common class but... I have allocated here for now
static void in_deinit(void) {
    padPortClose(0, 0);
    padPortClose(1, 0);
    padEnd();
}

/* common */
char cpu_clk_name[16] = "PS2 CPU clocks";

//Additional functions

void lprintf(const char *fmt, ...)
{
    va_list vl;
    
    va_start(vl, fmt);
    vprintf(fmt, vl);
    va_end(vl);
}

//stdio.h has this defined, but it's missing.
int setvbuf ( FILE * stream, char * buffer, int mode, size_t size ) {
    return -1;
}

// PLAT METHODS

int plat_get_root_dir(char *dst, int len) {
    if (len > 0) *dst = 0;
    return 0;
}

int plat_is_dir(const char *path) {
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

void *plat_mmap(unsigned long addr, size_t size) {
    void *ret;
    //    TODO FJTRUJY import mmap function
    //    void *req;
    //    req = (void *)addr;
    //    ret = mmap(req, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    //    if (ret == MAP_FAILED)
    //        return NULL;
    //    if (ret != req)
    //        printf("warning: mmaped to %p, requested %p\n", ret, req);
    ret = calloc(1, size);
    
    return ret;
}

void plat_munmap(void *ptr, size_t size) {
    //    TODO FJTRUJY import munmap function
    //    munmap(ptr, size);
    free(ptr);
}

void plat_video_menu_enter(int is_rom_loaded) {
    lprintf("Plat Video Menu Enter\n");
    // We need to re-create the FrameBufferTexture to refresh the content
    deinitFrameBufferTexture();
    initFrameBufferTexture();

    syncFrameBufferChache();    
    syncBackgroundChache();
}

void plat_video_menu_begin(void) {
    lprintf("Plat Video Menu Begin\n");
    resetFrameBufferTexture();
    clearGSGlobal();
    clearFrameBufferTexture();
    clearBackgroundTexture();
}

void plat_video_menu_end(void) {
    lprintf("Plat Video Menu End\n");
    syncFrameBufferChache();
    clearFrameBufferTexture();

    waitSemaphore();
    syncGSGlobalChache();
    /*
     FIXME: I don't know whether it's really a bug or not, but draw_menu_video_mode() fails to display text. I believe that it's because the GS is taking a while to receive/draw the uploaded frame buffer, and so the text stays on-screen for barely any time at all.
     Some things that makes that problem vanish:
     1. Adding a call to SyncFlipFB() at the end of ps2_redrawFrameBufferTexture().
     2. Invoking dmaKit_wait_fast below (Presumably forces the syste, to wait for the frame buffer to be transferred to VRAM).
     3. Instead of polling the pads directly, use the wait_for_input() function. It'll slow things down... but the text will somehow appear. This is how I arrived at my hypothesis that the menu frame buffer is spending nearly no time on-screen at all (When compared to the background).
     */
    dmaKit_wait_fast();
}

void plat_early_init(void) {
    /* Initilize the GS */
    initGSGlobal();

    SifInitRpc(0);
//    while(!SifIopReset(NULL, 0)){}; // Comment this line if you don't wanna debug the output

    ChangeThreadPriority(GetThreadId(), MAIN_THREAD_PRIORITY);

    initSemaphore();

    while(!SifIopSync()){};

}

void plat_init(void) {
    SifInitRpc(0);
    initModules();
    initBackgroundTexture();
    initFrameBufferTexture();
}

void plat_finish(void) {
    deinitPS2Textures();
    deinitSemaphore();
    in_deinit();
    deinitModules();

    SifExitRpc();
}

int plat_wait_event(int *fds_hnds, int count, int timeout_ms) {
    return -1;
}

void plat_sleep_ms(int ms) {
    delayMS(ms);
}

unsigned int plat_get_ticks_ms(void) {
    return ticksMS();
}

unsigned int plat_get_ticks_us(void) {
    return ticksUS();
}

void plat_wait_till_us(unsigned int us_to) {
    waitTillUS(us_to);
}

void plat_video_flip(void) {}

void plat_video_wait_vsync(void) {}

void plat_status_msg_clear(void) {}

void plat_status_msg_busy_next(const char *msg) {
    plat_status_msg_clear();
    pemu_finalize_frame("", msg);
    emu_status_msg("");
    
    /* assumption: msg_busy_next gets called only when
     * something slow is about to happen */
    reset_timing = 1;
}

void plat_status_msg_busy_first(const char *msg) {
    plat_status_msg_busy_next(msg);
}

void plat_update_volume(int has_changed, int is_up) {}

void plat_debug_cat(char *str) {
    strcat(str, is16BitsAccurate() ? "hard clut\n" : "soft clut\n");    //TODO: is this valid for this port?
}

const char *plat_get_credits(void) {
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
