#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include "ps2_semaphore.h"

static int VBlankStartSema;

// Private Methods

static int VBlankStartHandler(int cause) {
    ee_sema_t sema;
    iReferSemaStatus(VBlankStartSema, &sema);
    if(sema.count<sema.max_count) iSignalSema(VBlankStartSema);
    return 0;
}

// Public Methods

void initSemaphore(void) {
    ee_sema_t semaInfo;

    semaInfo.init_count = 0;
    semaInfo.max_count = 1;
    semaInfo.attr = 0;
    semaInfo.option = 0;
    
    VBlankStartSema=CreateSema(&semaInfo);

    AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
    EnableIntc(kINTC_VBLANK_START);
}

void waitSemaphore(void) {
    //Clear the semaphore to zero if it isn't already at zero, so that WaitSema will wait until the next VBlank start event.
    PollSema(VBlankStartSema);
    WaitSema(VBlankStartSema);
}

void deinitSemaphore(void) {
    DisableIntc(kINTC_VBLANK_START);
    RemoveIntcHandler(kINTC_VBLANK_START, 0);
    DeleteSema(VBlankStartSema);
}
