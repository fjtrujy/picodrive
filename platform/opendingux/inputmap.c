#include <stdio.h>
#include <string.h>
#include <SDL_keysym.h>

#include "../libpicofe/input.h"
#include "../libpicofe/in_sdl.h"
#include "../common/input_pico.h"
#include "../common/plat_sdl.h"

const struct in_default_bind in_sdl_defbinds[] = {
	{ SDLK_UP,	IN_BINDTYPE_PLAYER12, GBTN_UP },
	{ SDLK_DOWN,	IN_BINDTYPE_PLAYER12, GBTN_DOWN },
	{ SDLK_LEFT,	IN_BINDTYPE_PLAYER12, GBTN_LEFT },
	{ SDLK_RIGHT,	IN_BINDTYPE_PLAYER12, GBTN_RIGHT },
	{ SDLK_LSHIFT,	IN_BINDTYPE_PLAYER12, GBTN_A },
	{ SDLK_LALT,	IN_BINDTYPE_PLAYER12, GBTN_B },
	{ SDLK_LCTRL,	IN_BINDTYPE_PLAYER12, GBTN_C },
	{ SDLK_RETURN,	IN_BINDTYPE_PLAYER12, GBTN_START },
	{ SDLK_ESCAPE,	IN_BINDTYPE_EMU, PEVB_MENU },
	{ SDLK_TAB,		IN_BINDTYPE_EMU, PEVB_PICO_PPREV },
	{ SDLK_BACKSPACE,	IN_BINDTYPE_EMU, PEVB_PICO_PNEXT },
	{ SDLK_BACKSPACE,	IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
	{ SDLK_TAB,		IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
	{ SDLK_SPACE,	IN_BINDTYPE_EMU, PEVB_FF },
	{ 0, 0, 0 }
};

const struct menu_keymap in_sdl_key_map[] = {
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	{ SDLK_LCTRL,	PBTN_MOK },
	{ SDLK_LALT,	PBTN_MBACK },
	{ SDLK_SPACE,	PBTN_MA2 },
	{ SDLK_LSHIFT,	PBTN_MA3 },
	{ SDLK_TAB,	PBTN_L },
	{ SDLK_BACKSPACE,	PBTN_R },
};
const struct menu_keymap in_sdl_key_map_miyoo[] = {
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	{ SDLK_LALT,	PBTN_MOK }, // swapped A/B
	{ SDLK_LCTRL,	PBTN_MBACK },
	{ SDLK_SPACE,	PBTN_MA2 },
	{ SDLK_LSHIFT,	PBTN_MA3 },
	{ SDLK_TAB,	PBTN_L },
	{ SDLK_BACKSPACE,	PBTN_R },
};

const int in_sdl_key_map_sz = sizeof(in_sdl_key_map) / sizeof(in_sdl_key_map[0]);

const struct menu_keymap in_sdl_joy_map[] = {
	{ SDLK_UP,	PBTN_UP },
	{ SDLK_DOWN,	PBTN_DOWN },
	{ SDLK_LEFT,	PBTN_LEFT },
	{ SDLK_RIGHT,	PBTN_RIGHT },
	/* joystick */
	{ SDLK_WORLD_0,	PBTN_MOK },
	{ SDLK_WORLD_1,	PBTN_MBACK },
	{ SDLK_WORLD_2,	PBTN_MA2 },
	{ SDLK_WORLD_3,	PBTN_MA3 },
};
const int in_sdl_joy_map_sz = sizeof(in_sdl_joy_map) / sizeof(in_sdl_joy_map[0]);

const char * const _in_sdl_key_names[SDLK_LAST] = {
	[SDLK_UP] = "UP",
	[SDLK_DOWN] = "DOWN",
	[SDLK_LEFT] = "LEFT",
	[SDLK_RIGHT] = "RIGHT",
	[SDLK_RETURN] = "START",
	[SDLK_ESCAPE] = "SELECT",

	[SDLK_LCTRL] = "A",
	[SDLK_LALT] = "B",
	[SDLK_LSHIFT] = "Y",
	[SDLK_SPACE] = "X",

	[SDLK_TAB] = "L1", // up to 3 shoulder keys
	[SDLK_BACKSPACE] = "R1",
	[SDLK_PAGEUP] = "L2",
	[SDLK_PAGEDOWN] = "R2",
	[SDLK_KP_DIVIDE] = "L3",
	[SDLK_KP_PERIOD] = "R3",

	[SDLK_HOME] = "POWER", // additional POWER key
};
const char * const _in_sdl_key_names_gcw0[SDLK_LAST] = {
	[SDLK_UP] = "UP",
	[SDLK_DOWN] = "DOWN",
	[SDLK_LEFT] = "LEFT",
	[SDLK_RIGHT] = "RIGHT",
	[SDLK_RETURN] = "START",
	[SDLK_ESCAPE] = "SELECT",

	[SDLK_LCTRL] = "A",
	[SDLK_LALT] = "B",
	[SDLK_LSHIFT] = "X", // swapped X/Y
	[SDLK_SPACE] = "Y",

	[SDLK_TAB] = "L", // single shoulder keys
	[SDLK_BACKSPACE] = "R",

	[SDLK_POWER] = "POWER", // additional POWER/LOCK keys
	[SDLK_PAUSE] = "LOCK",
};
const char * const _in_sdl_key_names_miyoo[SDLK_LAST] = {
	[SDLK_UP] = "UP",
	[SDLK_DOWN] = "DOWN",
	[SDLK_LEFT] = "LEFT",
	[SDLK_RIGHT] = "RIGHT",
	[SDLK_RETURN] = "START",
	[SDLK_ESCAPE] = "SELECT",

	[SDLK_LCTRL] = "B", // swapped A/B
	[SDLK_LALT] = "A",
	[SDLK_LSHIFT] = "X", // swapped X/Y
	[SDLK_SPACE] = "Y",

	[SDLK_TAB] = "L1", // double shoulder keys
	[SDLK_BACKSPACE] = "R1",
	[SDLK_RALT] = "L2",
	[SDLK_RSHIFT] = "R2",

	[SDLK_RCTRL] = "R", // additional R key
};
const char * const (*in_sdl_key_names)[SDLK_LAST];

void init_sdl_keys(void)
{
	FILE *cmdfile = fopen("/proc/cmdline", "r");
	char cmdline[999];

	int n = fread(cmdline, 1, 999, cmdfile);
	cmdline[n] = '\0';

#ifdef __MIYOO__
	// miyoo
	memcpy(in_sdl_key_map, in_sdl_key_map_miyoo, sizeof(in_sdl_key_map));
	in_sdl_key_names = &_in_sdl_key_names_miyoo;
#else
	char *hw = strstr(cmdline, "hwvariant=");
	char *var = strtok(hw, " ");
	if (!var) 
		var = "v11_ddr2_256mb";
	if (!strcmp(var, "v11_ddr2_256mb") || !strcmp(var, "v20_mddr_512mb")) {
		// gcw0
		in_sdl_key_names = &_in_sdl_key_names_gcw0;
	} else {
		// rg350, rs90, rs97, ... 
		in_sdl_key_names = &_in_sdl_key_names;
	}
#endif
}
