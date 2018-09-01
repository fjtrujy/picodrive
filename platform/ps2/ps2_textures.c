#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
// #include <fileXio_rpc.h>
// #include <audsrv.h>
// #include <gsKit.h>
// #include <gsInline.h>

#include "ps2_textures.h"

// Utils Macros
// #define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00) // turn black GS Screen

GSGLOBAL *gsGlobal = NULL;

// GSGLOBAL *currentGSGlobal(void)
// {
//     return gsGlobal;
// }

void initGSGlobal(void)
{
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
}

// void clearGSGlobal(void)
// {
//     gsKit_clear(gsGlobal, GS_BLACK);
// }
    