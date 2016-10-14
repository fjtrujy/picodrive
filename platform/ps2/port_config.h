// port specific settings

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define CASE_SENSITIVE_FS 1
#define DONT_OPEN_MANY_FILES 1	// work around the open file limit of the various filesystems (and devices) that the PS2 supports.
#define REDUCE_IO_CALLS	1		//Like with the PSP, a high I/O access count is really bad for performance.
#define SIMPLE_WRITE_SOUND 0

#define SCREEN_SIZE_FIXED 1
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 224

// draw.c
#define USE_BGR555 1

// draw2.c
#define START_ROW  0 // which row of tiles to start rendering at?
#define END_ROW   28 // ..end

// pico.c
#define CAN_HANDLE_240_LINES	1

// logging emu events
#define EL_LOGMASK (EL_STATUS|EL_IDLE) // (EL_STATUS|EL_ANOMALY|EL_UIO|EL_SRAMIO) // xffff

//#define dprintf(f,...) printf("%05i:%03i: " f "\n",Pico.m.frame_count,Pico.m.scanline,##__VA_ARGS__)
#define dprintf(x...)

// platform
#define PATH_SEP      "/"
#define PATH_SEP_C    '/'
#define MENU_X2       0

#endif //PORT_CONFIG_H
