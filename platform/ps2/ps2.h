#define DEFAULT_PATH	"mass:"	//Only paths residing on "basic" devices (devices that don't require mounting) can be specified here, since this system doesn't perform mounting based on the path.
#define PATH_MAX FILENAME_MAX

void ps2_init(int argc, char *argv[]);
void ps2_finish(void);

void FlipFBNoSync(void);
void SyncFlipFB(void);

void ps2_SetAudioFormat(unsigned int rate);

unsigned int ps2_pad_read(int port, int slot);
unsigned int ps2_pad_read_all(void);
unsigned int ps2_GetTicksInUsec(void);
void DelayThread(unsigned short int msec);

void ps2_loadHDDModules(void);

enum PS2_DISPLAY_MODE{
	PS2_DISPLAY_MODE_AUTO,
	PS2_DISPLAY_MODE_NTSC,
	PS2_DISPLAY_MODE_PAL,
	PS2_DISPLAY_MODE_480P,
	PS2_DISPLAY_MODE_NTSC_NI,
	PS2_DISPLAY_MODE_PAL_NI,

	PS2_DISPLAY_MODE_COUNT
};
void ps2_SetDisplayMode(int mode);

extern void *ps2_screen;
extern unsigned short int ps2_screen_width, ps2_screen_height;

void ps2_ClearScreen(void);
void ps2_DrawFrameBuffer(float u1, float v1, float u2, float v2);

#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00)

//Thread priorities (lower = higher priority)
#define MAIN_THREAD_PRIORITY	0x51
#define SOUND_THREAD_PRIORITY	0x50

/* shorter btn names */
#define PBTN_UP       PAD_UP
#define PBTN_LEFT     PAD_LEFT
#define PBTN_RIGHT    PAD_RIGHT
#define PBTN_DOWN     PAD_DOWN
#define PBTN_L1       PAD_L1
#define PBTN_R1       PAD_R1
#define PBTN_L2       PAD_L2
#define PBTN_R2       PAD_R2
#define PBTN_L3       PAD_L3
#define PBTN_R3       PAD_R3
#define PBTN_TRIANGLE PAD_TRIANGLE
#define PBTN_CIRCLE   PAD_CIRCLE
#define PBTN_X        PAD_CROSS
#define PBTN_SQUARE   PAD_SQUARE
#define PBTN_SELECT   PAD_SELECT
#define PBTN_START    PAD_START

/* fake 'nub' btns */
#define PBTN_NUB_L_UP	0x01000000
#define PBTN_NUB_L_RIGHT 0x02000000
#define PBTN_NUB_L_DOWN  0x04000000
#define PBTN_NUB_L_LEFT  0x08000000
#define PBTN_NUB_R_UP    0x10000000
#define PBTN_NUB_R_RIGHT 0x20000000
#define PBTN_NUB_R_DOWN  0x40000000
#define PBTN_NUB_R_LEFT  0x80000000

void lprintf(const char *fmt, ...);

#define ALLOW_16B_RENDERER_USE	1	//Uncomment to allow users to select the 16-bit accurate renderer. It has no real purpose (Other than for taking screencaps), since it's slower than the 8-bit renderers.

//Add-on I/O functions from io_suppliment.c:
int ps2_remove(const char *file);
int ps2_mkdir(const char *path, int mode);
