EE_BIN = PicoDrive.elf

#Options
use_musashi = 0
use_mz80 = 0

EE_GPVAL = -G0
EE_CFLAGS += -mno-gpopt -O3 $(EE_GPVAL) -DNO_SYNC -DUTYPES_DEFINED
EE_INCS += -I../.. -I. -I$(PS2SDK)/ports/include -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LDFLAGS += -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ee/lib -Tlinkfile -s
EE_LIBS += -lgskit -ldmakit -lpng -laudsrv -lpad -lm -lz -lcdvd -lmad -lfileXio -lpatches -lc -lkernel
EE_IOP_OBJS = IOMANX_irx.o FILEXIO_irx.o LIBSD_irx.o AUDSRV_irx.o USBD_irx.o USBHDFSD_irx.o POWEROFF_irx.o DEV9_irx.o ATAD_irx.o HDD_irx.o PFS_irx.o

# frontend and stuff
EE_OBJS := ps2.o io_suppliment.o emu.o menu.o mp3.o asm_utils.o $(EE_IOP_OBJS) main.o

# common
EE_OBJS += ../common/emu.o ../common/config.o ../common/menu.o ../common/fonts.o ../common/readpng.o

# Pico
EE_OBJS += ../../Pico/Area.o ../../Pico/Cart.o ../../Pico/Debug.o ../../Pico/Memory.o ../../Pico/Misc.o \
		../../Pico/Pico/Pico.o ../../Pico/Pico/Memory.o ../../Pico/Pico/xpcm.o	\
		../../Pico/Pico.o ../../Pico/Sek.o ../../Pico/VideoPort.o ../../Pico/Draw2.o ../../Pico/Draw.o \
		../../Pico/Patch.o ../../Pico/Draw_mips_r5900.o ../../Pico/Memory_mips_r5900.o ../../Pico/Misc_mips_r5900.o
# Pico - CD
EE_OBJS += ../../Pico/cd/Pico.o ../../Pico/cd/Memory.o ../../Pico/cd/Sek.o ../../Pico/cd/LC89510.o \
		../../Pico/cd/cd_sys.o ../../Pico/cd/cd_file.o ../../Pico/cd/gfx_cd.o \
		../../Pico/cd/Area.o ../../Pico/cd/Misc.o ../../Pico/cd/pcm.o ../../Pico/cd/buffering.o ../../Pico/cd/cue.o

# Pico - CartHW
EE_OBJS += ../../Pico/carthw/carthw.o ../../Pico/carthw/svp/svp.o ../../Pico/carthw/svp/ssp16.o ../../Pico/carthw/svp/Memory.o ../../Pico/carthw/svp/compiler.o

# Pico - sound
EE_OBJS += ../../Pico/sound/sound.o ../../Pico/sound/mix.o ../../Pico/sound/sn76496.o ../../Pico/sound/ym2612.o
# zlib (hacked)
EE_OBJS += ../../zlib/gzio.o ../../zlib/inffast.o ../../zlib/inflate.o ../../zlib/inftrees.o ../../zlib/trees.o \
	../../zlib/deflate.o ../../zlib/crc32.o ../../zlib/adler32.o ../../zlib/zutil.o ../../zlib/compress.o \
	../../zlib/uncompr.o
# unzip
EE_OBJS += ../../unzip/unzip.o ../../unzip/unzip_stream.o
# CPU cores
ifeq "$(use_musashi)" "1"
EE_CFLAGS += -DEMU_M68K
EE_OBJS += ../../cpu/musashi/m68kops.o ../../cpu/musashi/m68kcpu.o
else
EE_CFLAGS += -DEMU_F68K
EE_OBJS += ../../cpu/fame/famec.o
endif
# z80
ifeq "$(use_mz80)" "1"
EE_CFLAGS += -D_USE_MZ80
EE_OBJS += ../../cpu/mz80/mz80.o
else
EE_CFLAGS += -D_USE_CZ80
EE_OBJS += ../../cpu/cz80/cz80.o
endif

%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

$(EE_BIN) : $(EE_OBJS)
	$(EE_CC) $(EE_CFLAGS) $(EE_LDFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LIBS)

all: $(EE_BIN)

USBD_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/usbd.irx USBD_irx.o USBD_irx
USBHDFSD_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/usbhdfsd.irx USBHDFSD_irx.o USBHDFSD_irx
LIBSD_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/freesd.irx LIBSD_irx.o LIBSD_irx
AUDSRV_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/audsrv.irx AUDSRV_irx.o AUDSRV_irx
IOMANX_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/iomanX.irx IOMANX_irx.o IOMANX_irx
FILEXIO_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/fileXio.irx FILEXIO_irx.o FILEXIO_irx
POWEROFF_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/poweroff.irx POWEROFF_irx.o POWEROFF_irx
DEV9_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/ps2dev9.irx DEV9_irx.o DEV9_irx
ATAD_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/ps2atad.irx ATAD_irx.o ATAD_irx
HDD_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/ps2hdd.irx HDD_irx.o HDD_irx
PFS_irx.o:
	bin2o $(EE_GPVAL) $(PS2SDK)/iop/irx/ps2fs.irx PFS_irx.o PFS_irx

../../cpu/musashi/m68kops.c :
	make -C ../../cpu/musashi

../../cpu/fame/famec.o : ../../cpu/fame/famec.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Wno-unused -c $< -o $@

../../Pico/Memory.o : ../../Pico/Memory.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@ -D_ASM_MEMORY_C -D_ASM_MEMORY_C_AMIPS

../../Pico/cd/Memory.o : ../../Pico/cd/Memory.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

../../Pico/Draw.o : ../../Pico/Draw.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@ -D_ASM_DRAW_C_AMIPS

../../Pico/Misc.o : ../../Pico/Misc.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@ -D_ASM_MISC_C_AMIPS

../../Pico/cd/gfx_cd.o : ../../Pico/cd/gfx_cd.c
	@echo ">>>" $<
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref