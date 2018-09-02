#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>

#include "ps2_timing.h"
#include "ps2_textures.h"

// PRIVATE METHODS

static unsigned int cpuMHZ(void)
{
    return 295;
}

static void threadWakeupCB(s32 alarm_id, u16 time, void *arg2){
    iWakeupThread(*(int*)arg2);
}

static unsigned int mSec2HSyncTicks(unsigned int msec){
    return msec*currentDisplayMode->HsyncsPerMsec;
}

// PUBLIC METHODS

unsigned int ticksUS(void)
{
    //    return(clock()/(CLOCKS_PER_SEC*1000000UL));    //Broken.
    return cpu_ticks()/cpuMHZ();
}

unsigned int ticksMS(void)
{
    return ticksUS()/1000;
}

void delayMS(unsigned short int msec)
{
    int ThreadID;

    if(msec>0){
        ThreadID=GetThreadId();
        SetAlarm(mSec2HSyncTicks(msec), &threadWakeupCB, &ThreadID);
        SleepThread();
    }
}

void delayCycles(unsigned short int cycles)
{
    return delayMS(cycles/cpuMHZ());
}

void waitTillUS(unsigned int us_to)
{
    unsigned int now, diff;
    diff = (us_to-ticksUS())/1000;
    
    if (diff > 0 && diff < 50 ) { // This maximum is to avoid the restart cycle of the PS2 cpu_ticks
        delayMS(diff);
    }
}
