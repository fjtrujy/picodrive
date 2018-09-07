#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <io_common.h>
#include <loadfile.h>
#include <unistd.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <audsrv.h>

#include "ps2_modules.h"

// Private variables

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

extern unsigned char poweroff_irx_start[];
extern unsigned int poweroff_irx_size;

extern unsigned char ps2dev9_irx_start[];
extern unsigned int ps2dev9_irx_size;

extern unsigned char ps2atad_irx_start[];
extern unsigned int ps2atad_irx_size;

extern unsigned char ps2hdd_irx_start[];
extern unsigned int ps2hdd_irx_size;

extern unsigned char ps2fs_irx_start[];
extern unsigned int ps2fs_irx_size;

extern unsigned char iomanX_irx_start[];
extern unsigned int iomanX_irx_size;

extern unsigned char fileXio_irx_start[];
extern unsigned int fileXio_irx_size;

extern unsigned char freesd_irx_start[];
extern unsigned int freesd_irx_size;

extern unsigned char audsrv_irx_start[];
extern unsigned int audsrv_irx_size;

extern unsigned char usbd_irx_start[];
extern unsigned int usbd_irx_size;

extern unsigned char usbhdfsd_irx_start[];
extern unsigned int usbhdfsd_irx_size;

static unsigned char HDDModulesLoaded=0;

#define DEFAULT_PATH    "mass:"    //Only paths residing on "basic" devices (devices that don't require mounting) can be specified here, since this system doesn't perform mounting based on the path.

// Private Methods

static const char *getMountParams(const char *command, char *BlockDevice) {
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

static void loadIOPModules(void) {
    SifExecModuleBuffer(iomanX_irx_start, iomanX_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(fileXio_irx_start, fileXio_irx_size, 0, NULL, NULL);
    
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:MCMAN", 0, NULL);
    SifLoadModule("rom0:MCSERV", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    
    SifExecModuleBuffer(usbd_irx_start, usbd_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(usbhdfsd_irx_start, usbhdfsd_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(freesd_irx_start, freesd_irx_size, 0, NULL, NULL);
    SifExecModuleBuffer(audsrv_irx_start, audsrv_irx_size, 0, NULL, NULL);
}

static int getBootDeviceID(const char *path) {
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

//HACK! If booting from a USB device, keep trying to open this program again until it succeeds. This will ensure that the emulator will be able to load its files.
static void waitUntilDeviceIsReady(const char *path) {
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

void setPWDOnPFS(const char *FullCWD_path) {
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

// Public Methods

void loadHDDModules(void) {
    /* Try not to adjust this unless you know what you are doing. The tricky part i keeping the NULL character in the middle of that argument list separated from the number 4. */
    static const char PS2HDD_args[]="-o\0""2";
    static const char PS2FS_args[]="-o\0""8";
    
    if(!HDDModulesLoaded){
        SifExecModuleBuffer(poweroff_irx_start, poweroff_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(ps2dev9_irx_start, ps2dev9_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(ps2atad_irx_start, ps2atad_irx_size, 0, NULL, NULL);
        SifExecModuleBuffer(ps2hdd_irx_start, ps2hdd_irx_size, sizeof(PS2HDD_args), PS2HDD_args, NULL);
        SifExecModuleBuffer(ps2fs_irx_start, ps2fs_irx_size, sizeof(PS2FS_args), PS2FS_args, NULL);
        HDDModulesLoaded=1;
    }
}

void initModules(void) {
    char cwd[FILENAME_MAX], blockDevice[16];
    const char *mountPoint;
    int bootDeviceID;
    
    sbv_patch_enable_lmb();
    
    loadIOPModules();
    fileXioInit();
    audsrv_init();

    //TODO: I DONT KNOW YET, WHY IT CRASHES IF UNCOMENT THIS PART 
    getcwd(cwd, sizeof(cwd));
    bootDeviceID=getBootDeviceID(cwd);   
    //Mount the HDD partition, if required.
    if(bootDeviceID==BOOT_DEVICE_HDD){
        loadHDDModules();
        
        //Attempt to mount the partition.
        if((mountPoint=getMountParams(cwd, blockDevice))!=NULL && !strncmp(mountPoint, "pfs:", 4)){
            fileXioMount("pfs0:", blockDevice, FIO_MT_RDWR);
            
            setPWDOnPFS(&mountPoint[4]);
        }
    } else if(bootDeviceID==BOOT_DEVICE_CDROM){
        chdir(DEFAULT_PATH);
    } else if(bootDeviceID==BOOT_DEVICE_MASS){
        // waitUntilDeviceIsReady(argv[0]);
    } else if (bootDeviceID==BOOT_DEVICE_UNKNOWN) {

    }
}

void deinitModules(void) {
    fileXioUmount("pfs0:");

    fileXioExit();
}
