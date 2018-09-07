include $(PS2SDK)/samples/Makefile.pref 

#folders binaries & binaries
EE_BIN2O = bin2o
IRX_DIR = $(PS2SDK)/iop/irx
EE_BIN = PicoDrive.elf

# settings
use_musashi = 0
use_mz80 = 0
asm_memory = 1
use_sh2mame = 1
use_DEBUG = 1

ifeq ($(use_DEBUG), 1)
EE_CFLAGS += -g
# Needed for *.s files
EE_ASFLAGS += -g
else
EE_LDFLAGS += -s
endif

# Linker flags
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ee/lib -Tlinkfile
# C compiler flags
EE_CFLAGS += -mno-gpopt -O3 -G0 -DNO_SYNC -DUTYPES_DEFINED -D_ASM_DRAW_C_AMIPS -DIN_PS2

# Include directories
EE_INCS += -I../.. -I. -I$(PS2SDK)/ports/include -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
# Link with following libraries.
EE_LIBS += -lgskit -ldmakit -lpng -laudsrv -lpad -lm -lz -lcdvd -lmad -lfileXio -lpatches -lc -lkernel

#IRX modules
# IRX modules - modules have to be in IRX_DIR
EE_IRX = iomanX.irx fileXio.irx freesd.irx audsrv.irx usbd.irx usbhdfsd.irx poweroff.irx ps2dev9.irx ps2atad.irx ps2hdd.irx ps2fs.irx
EE_IRX_OBJ = $(EE_IRX:.irx=.o)
EE_OBJS += $(EE_IRX_OBJ)

# frontend and stuff
EE_OBJS += emu.o plat.o mp3.o in_ps2.o
# fronted utils
EE_OBJS += utils/ps2_textures.o utils/ps2_timing.o utils/ps2_semaphore.o utils/ps2_config.o utils/ps2_modules.o \
		utils/ps2_pico.o utils/ps2_drawer.o  utils/ps2_sound.o utils/asm.o utils/io_suppliment.o

# common
EE_OBJS += platform/common/emu.o platform/common/menu.o platform/common/fonts.o platform/common/config.o \
	 	platform/common/readpng.o platform/common/input.o platform/common/main.o

# Pico
EE_OBJS += pico/area.o pico/cart.o pico/debug.o pico/memory.o pico/misc.o \
		pico/pico/pico.o pico/pico/memory.o pico/pico/xpcm.o	\
		pico/pico.o pico/sek.o pico/videoport.o pico/draw2.o pico/draw.o \
		pico/patch.o pico/z80if.o pico/sms.o pico/mode4.o pico/eeprom.o \
        pico/draw_mips_r5900.o pico/memory_mips_r5900.o pico/misc_mips_r5900.o

# Pico - CD
EE_OBJS += pico/cd/pico.o pico/cd/memory.o pico/cd/sek.o pico/cd/LC89510.o \
		pico/cd/cd_sys.o pico/cd/cd_file.o pico/cd/gfx_cd.o \
		pico/cd/area.o pico/cd/misc.o pico/cd/pcm.o pico/cd/buffering.o pico/cd/cue.o

# Pico - 32X
EE_OBJS += pico/32x/32x.o pico/32x/memory.o pico/32x/draw.o pico/32x/pwm.o

# Pico - CartHW
EE_OBJS += pico/carthw/carthw.o pico/carthw/svp/svp.o pico/carthw/svp/ssp16.o pico/carthw/svp/memory.o pico/carthw/svp/compiler.o

# Pico - sound
EE_OBJS += pico/sound/sound.o pico/sound/mix.o pico/sound/sn76496.o pico/sound/ym2612.o

# zlib (hacked)
EE_OBJS += 	zlib/gzio.o zlib/inffast.o zlib/inflate.o zlib/inftrees.o zlib/trees.o \
		zlib/deflate.o zlib/crc32.o zlib/adler32.o zlib/zutil.o zlib/compress.o \
		zlib/uncompr.o

# unzip
EE_OBJS += unzip/unzip.o unzip/unzip_stream.o

# CPU cores
ifeq "$(use_musashi)" "1"
EE_CFLAGS += -DEMU_M68K
EE_OBJS += cpu/musashi/m68kops.o cpu/musashi/m68kcpu.o
else
EE_CFLAGS += -DEMU_F68K
EE_OBJS += cpu/fame/famec.o
endif

# z80
ifeq "$(use_mz80)" "1"
EE_CFLAGS += -D_USE_MZ80
EE_OBJS += cpu/mz80/mz80.o
else
EE_CFLAGS += -D_USE_CZ80
EE_OBJS += cpu/cz80/cz80.o
endif

# sh2
EE_OBJS += cpu/sh2/sh2.o
ifeq "$(use_sh2mame)" "1"
EE_OBJS += cpu/sh2/mame/sh2pico.o
else
endif

# CMN

EE_OBJS += cpu/drc/cmn.o

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

#Define RULES per specific kind of files
%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

PicoDrive: $(EE_OBJS)
	$(EE_CC) $(EE_CFLAGS) $(EE_LDFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LIBS)

mkdirs:
	@mkdir -p $(DIRS)

#Specific file name and output per IRX Module
$(EE_IRX_OBJ):
	$(EE_BIN2O) $(IRX_DIR)/$(@:.o=.irx) $@ $(@:.o=_irx)


#Specific rules for pico/misc.c and pico/memory.c
pico/misc.o : pico/misc.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@ -D_ASM_MISC_C_AMIPS

pico/memory.o : pico/memory.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@ -D_ASM_MEMORY_C_AMIPS

clean:
	rm -f $(EE_BIN) $(EE_IRX_OBJ) $(EE_OBJS)
	rm -rf $(DIRS)

debug:
	ps2client -h 192.168.1.150 netdump
	ps2client -h 192.168.1.150 execee host:$(EE_BIN)

reset:
	ps2client -h 192.168.1.150 reset

restart: reset all debug

epc:
	ee-addr2line -e $(EE_BIN) $(line)
