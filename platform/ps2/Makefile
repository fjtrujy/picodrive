#folders binaries & binaries
CC = ee-gcc
BIN2O = bin2o
IOP_IRX_FOLDER = $(PS2SDK)/iop/irx
EE_BIN = PicoDrive.elf

# settings
use_musashi = 0
#use_fame = 1
use_mz80 = 0
#profile = 1
asm_memory = 1
use_sh2mame = 1

ifeq "$(profile)" "1"
GPVAL = -G0
LDFLAGS += -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ee/lib -Tlinkfile -s
CFLAGS += -mno-gpopt -O3 $(GPVAL) -DNO_SYNC -DUTYPES_DEFINED -D_ASM_DRAW_C_AMIPS -DIN_PS2
else
GPVAL = -G0
LDFLAGS += -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ee/lib -Tlinkfile -s
CFLAGS += -mno-gpopt -O3 $(GPVAL) -DNO_SYNC -DUTYPES_DEFINED -D_ASM_DRAW_C_AMIPS -g -DIN_PS2
endif

INCS += -I../.. -I. -I$(PS2SDK)/ports/include -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
LIBS += -lgskit -ldmakit -lpng -laudsrv -lpad -lm -lz -lcdvd -lmad -lfileXio -lpatches -lc -lkernel

#IRX modules
IOP_OBJS = IOMANX_irx.o FILEXIO_irx.o LIBSD_irx.o AUDSRV_irx.o USBD_irx.o USBHDFSD_irx.o POWEROFF_irx.o DEV9_irx.o ATAD_irx.o HDD_irx.o PFS_irx.o
OBJS += $(IOP_OBJS)

# frontend and stuff
OBJS += emu.o plat.o mp3.o in_ps2.o ps2_textures.o ps2_timing.o utils/asm.o utils/io_suppliment.o

# common
OBJS += platform/common/emu.o platform/common/menu.o platform/common/fonts.o platform/common/config.o \
	 	platform/common/readpng.o platform/common/input.o platform/common/main.o

# Pico
OBJS += pico/area.o pico/cart.o pico/debug.o ../../pico/memory.o ../../pico/misc.o \
		pico/pico/pico.o pico/pico/memory.o pico/pico/xpcm.o	\
		pico/pico.o pico/sek.o pico/videoport.o pico/draw2.o pico/draw.o \
		pico/patch.o pico/z80if.o pico/sms.o pico/mode4.o pico/eeprom.o \
        pico/draw_mips_r5900.o pico/memory_mips_r5900.o pico/misc_mips_r5900.o

# Pico - CD
OBJS += pico/cd/pico.o pico/cd/memory.o pico/cd/sek.o pico/cd/LC89510.o \
		pico/cd/cd_sys.o pico/cd/cd_file.o pico/cd/gfx_cd.o \
		pico/cd/area.o pico/cd/misc.o pico/cd/pcm.o pico/cd/buffering.o pico/cd/cue.o

# Pico - 32X
OBJS += pico/32x/32x.o pico/32x/memory.o pico/32x/draw.o pico/32x/pwm.o

# Pico - CartHW
OBJS += pico/carthw/carthw.o pico/carthw/svp/svp.o pico/carthw/svp/ssp16.o pico/carthw/svp/memory.o pico/carthw/svp/compiler.o

# Pico - sound
OBJS += pico/sound/sound.o pico/sound/mix.o pico/sound/sn76496.o pico/sound/ym2612.o

# zlib (hacked)
OBJS += zlib/gzio.o zlib/inffast.o zlib/inflate.o zlib/inftrees.o zlib/trees.o \
		zlib/deflate.o zlib/crc32.o zlib/adler32.o zlib/zutil.o zlib/compress.o \
		zlib/uncompr.o

# unzip
OBJS += unzip/unzip.o unzip/unzip_stream.o

# CPU cores
ifeq "$(use_musashi)" "1"
CFLAGS += -DEMU_M68K
OBJS += cpu/musashi/m68kops.o cpu/musashi/m68kcpu.o
else
CFLAGS += -DEMU_F68K
OBJS += cpu/fame/famec.o
endif

# z80
ifeq "$(use_mz80)" "1"
CFLAGS += -D_USE_MZ80
OBJS += cpu/mz80/mz80.o
else
CFLAGS += -D_USE_CZ80
OBJS += cpu/cz80/cz80.o
endif

# sh2
OBJS += cpu/sh2/sh2.o
ifeq "$(use_sh2mame)" "1"
OBJS += cpu/sh2/mame/sh2pico.o
else
endif

# CMN
OBJS += cpu/drc/cmn.o


# DIRS to add and check file
vpath %.c = ../..
vpath %.s = ../..
vpath %.S = ../..

DIRS = platform platform/ps2 platform/common pico pico/cd pico/pico pico/sound pico/carthw/svp \
    pico/32x zlib unzip cpu cpu/musashi cpu/fame cpu/mz80 cpu/cz80 cpu/sh2/mame cpu/drc

all: mkdirs PicoDrive

#include ../common/common.mak
#include ../common/common_arm.mak
include ../common/revision.mak

%.o : %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

%.o : %.s
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

PicoDrive : $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(EE_BIN) $(OBJS) $(LIBS)

mkdirs:
	@mkdir -p $(DIRS)

#Specific file name and output per IRX Module
USBD_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/usbd.irx $@ USBD_irx
USBHDFSD_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/usbhdfsd.irx $@ USBHDFSD_irx
LIBSD_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/freesd.irx $@ LIBSD_irx
AUDSRV_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/audsrv.irx $@ AUDSRV_irx
IOMANX_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/iomanX.irx $@ IOMANX_irx
FILEXIO_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/fileXio.irx $@ FILEXIO_irx
POWEROFF_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/poweroff.irx $@ POWEROFF_irx
DEV9_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/ps2dev9.irx $@ DEV9_irx
ATAD_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/ps2atad.irx $@ ATAD_irx
HDD_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/ps2hdd.irx $@ HDD_irx
PFS_irx.o:
	$(BIN2O) $(IOP_GPVAL) $(IOP_IRX_FOLDER)/ps2fs.irx $@ PFS_irx



../../cpu/musashi/m68kops.c :
	make -C ../../cpu/musashi

../../cpu/fame/famec.o : ../../cpu/fame/famec.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) $(INCS) -Wno-unused -c $< -o $@

../../pico/misc.o : ../../pico/misc.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@ -D_ASM_MISC_C_AMIPS

../../pico/memory.o : ../../pico/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@ -D_ASM_MEMORY_C_AMIPS

../../pico/cd/memory.o : ../../pico/cd/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

../../pico/cd/gfx_cd.o : ../../pico/cd/gfx_cd.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

clean:
	rm -f $(EE_BIN) $(OBJS)
	rm -rf $(DIRS)

debug:
	ps2client -h 192.168.1.150 execee host:PicoDrive.elf

reset:
	ps2client -h 192.168.1.150 reset

restart: reset all debug