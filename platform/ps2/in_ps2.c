//
//  in_ps2.c
//  PicoDrive
//
//  Created by Francisco Javier Trujillo Mata on 03/10/16.
//  Copyright © 2016 Francisco Trujillo. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "../common/input.h"
#include "in_ps2.h"

#define IN_PREFIX "ps2:"
#define IN_PS2_NBUTTONS 32

/* note: in_ps2 hadles combos (if 2 btns have the same bind,
 * both must be pressed for action to happen) */
static int in_ps2_combo_keys = 0;
static int in_ps2_combo_acts = 0;
static int gpiodev = -1;	/* Wiz only */


enum  { BTN_UP = 4,      BTN_LEFT = 7,      BTN_DOWN = 6,  BTN_RIGHT = 5,
    BTN_START = 3,   BTN_SELECT = 0,    BTN_L1 = 10,    BTN_R1 = 11,
    BTN_X = 14,      BTN_SQUARE = 15,        BTN_CIRCLE = 13,    BTN_TRIANGLE = 12,
    BTN_L2 = 8,     BTN_R2 = 9,       BTN_L3 = 1,            BTN_R3 = 2
    };

static const char * const in_ps2_prefix = IN_PREFIX;
static const char *in_ps2_keys[IN_PS2_NBUTTONS] = {
    [0 ... IN_PS2_NBUTTONS-1] = NULL,
    [BTN_UP]    = "UP",    [BTN_LEFT]   = "LEFT",   [BTN_DOWN] = "DOWN", [BTN_RIGHT] = "RIGHT",
    [BTN_START] = "START", [BTN_SELECT] = "SELECT", [BTN_L1]    = "L",    [BTN_R1]     = "R",
    [BTN_X]     = "X",     [BTN_SQUARE] = "SQUARE",      [BTN_CIRCLE] = "CIRCLE",    [BTN_TRIANGLE] = "TRIANGLE"
};

static int in_ps2_get_bits(void)
{
    return ps2_pad_read_all();
}

static int in_ps2_get_wiz_bits(void)
{
    int value = 0;
    read(gpiodev, &value, 4);
    if (value & 0x02)
        value |= 0x05;
    if (value & 0x08)
        value |= 0x14;
    if (value & 0x20)
        value |= 0x50;
    if (value & 0x80)
        value |= 0x41;
    
    /* convert to PS2 style */
    value &= 0x7ff55;
    if (value & (1 << 16))
//        value |= 1 << BTN_VOL_UP;
    if (value & (1 << 17))
//        value |= 1 << BTN_VOL_DOWN;
    if (value & (1 << 18))
//        value |= 1 << BTN_PUSH;
    value &= ~0x70000;
    
    return value;
}

#ifdef FAKE_IN_PS2
static int in_ps2_get_fake_bits(void)
{
    extern int current_keys;
    return current_keys;
}
#endif

static void in_ps2_probe(void)
{
//    ps2_soc_t soc;
//    
//    soc = soc_detect();
//    switch (soc)
//    {
//        case SOCID_MMSP2:
//            in_ps2_get_bits = in_ps2_get_mmsp2_bits;
//            break;
//        case SOCID_POLLUX:
//            gpiodev = open("/dev/GPIO", O_RDONLY);
//            if (gpiodev < 0) {
//                perror("in_ps2: couldn't open /dev/GPIO");
//                return;
//            }
//            in_ps2_get_bits = in_ps2_get_wiz_bits;
//            break;
//        default:
//#ifdef FAKE_IN_PS2
//            in_ps2_get_bits = in_ps2_get_fake_bits;
//            break;
//#endif
//            return;
//    }
    
    in_register(IN_PREFIX "PS2 pad", IN_DRVID_PS2, -1, (void *)1,
                IN_PS2_NBUTTONS, 1);
}

static void in_ps2_free(void *drv_data)
{
    if (gpiodev >= 0) {
        close(gpiodev);
        gpiodev = -1;
    }
}

static int in_ps2_get_bind_count(void)
{
    return IN_PS2_NBUTTONS;
}

/* ORs result with pressed buttons */
int in_ps2_update(void *drv_data, const int *binds, int *result)
{
    int type_start = 0;
    int i, t, keys;
    
    keys = in_ps2_get_bits();
    
//    if (keys & in_ps2_combo_keys) {
//        result[IN_BINDTYPE_EMU] = in_combos_do(keys, binds, BTN_PUSH,
//                                               in_ps2_combo_keys, in_ps2_combo_acts);
//        type_start = IN_BINDTYPE_PLAYER12;
//    }
    
    for (i = 0; keys; i++, keys >>= 1) {
        if (!(keys & 1))
            continue;
        
        for (t = type_start; t < IN_BINDTYPE_COUNT; t++)
            result[t] |= binds[IN_BIND_OFFS(i, t)];
    }
    
    return 0;
}

int in_ps2_update_keycode(void *data, int *is_down)
{
    static int old_val = 0;
    int val, diff, i;
    
    val = in_ps2_get_bits();
    diff = val ^ old_val;
    if (diff == 0)
        return -1;
    
    /* take one bit only */
    for (i = 0; i < sizeof(diff)*8; i++)
        if (diff & (1<<i))
            break;
    
    old_val ^= 1 << i;
    
    if (is_down)
        *is_down = !!(val & (1<<i));
    return i;
}

static const struct {
    short key;
    short pbtn;
} key_pbtn_map[] =
{
    { BTN_UP,	PBTN_UP },
    { BTN_DOWN,	PBTN_DOWN },
    { BTN_LEFT,	PBTN_LEFT },
    { BTN_RIGHT,	PBTN_RIGHT },
    { BTN_CIRCLE,	PBTN_MOK },
    { BTN_X,	PBTN_MBACK },
    { BTN_SQUARE,	PBTN_MA2 },
    { BTN_TRIANGLE,	PBTN_MA3 },
    { BTN_L1,	PBTN_L },
    { BTN_R1,	PBTN_R },
    { BTN_SELECT,	PBTN_MENU },
};

#define KEY_PBTN_MAP_SIZE (sizeof(key_pbtn_map) / sizeof(key_pbtn_map[0]))

static int in_ps2_menu_translate(int keycode)
{
    int i;
    if (keycode < 0)
    {
        /* menu -> kc */
        keycode = -keycode;
        for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
            if (key_pbtn_map[i].pbtn == keycode)
                return key_pbtn_map[i].key;
    }
    else
    {
        for (i = 0; i < KEY_PBTN_MAP_SIZE; i++)
            if (key_pbtn_map[i].key == keycode)
                return key_pbtn_map[i].pbtn;
    }
    
    return 0;
}

static int in_ps2_get_key_code(const char *key_name)
{
    int i;
    
    for (i = 0; i < IN_PS2_NBUTTONS; i++) {
        const char *k = in_ps2_keys[i];
        if (k != NULL && strcasecmp(k, key_name) == 0)
            return i;
    }
    
    return -1;
}

static const char *in_ps2_get_key_name(int keycode)
{
    const char *name = NULL;
    if (keycode >= 0 && keycode < IN_PS2_NBUTTONS)
        name = in_ps2_keys[keycode];
    if (name == NULL)
        name = "Unkn";
    
    return name;
}

static const struct {
    short code;
    char btype;
    char bit;
} in_ps2_def_binds[] =
{
    /* MXYZ SACB RLDU */
    { BTN_UP,	IN_BINDTYPE_PLAYER12, 0 },
    { BTN_DOWN,	IN_BINDTYPE_PLAYER12, 1 },
    { BTN_LEFT,	IN_BINDTYPE_PLAYER12, 2 },
    { BTN_RIGHT,	IN_BINDTYPE_PLAYER12, 3 },
    { BTN_SQUARE,	IN_BINDTYPE_PLAYER12, 4 },	/* B */
    { BTN_X,	IN_BINDTYPE_PLAYER12, 5 },	/* C */
    { BTN_CIRCLE,	IN_BINDTYPE_PLAYER12, 6 },	/* A */
    { BTN_START,	IN_BINDTYPE_PLAYER12, 7 },
    { BTN_SELECT,	IN_BINDTYPE_EMU, PEVB_MENU },
    //	{ BTN_Y,	IN_BINDTYPE_EMU, PEVB_SWITCH_RND },
    { BTN_L1,	IN_BINDTYPE_EMU, PEVB_STATE_SAVE },
    { BTN_R1,	IN_BINDTYPE_EMU, PEVB_STATE_LOAD },
//    { BTN_VOL_UP,	IN_BINDTYPE_EMU, PEVB_VOL_UP },
//    { BTN_VOL_DOWN,	IN_BINDTYPE_EMU, PEVB_VOL_DOWN },
};

#define DEF_BIND_COUNT (sizeof(in_ps2_def_binds) / sizeof(in_ps2_def_binds[0]))

static void in_ps2_get_def_binds(int *binds)
{
    int i;
    
    for (i = 0; i < DEF_BIND_COUNT; i++)
        binds[IN_BIND_OFFS(in_ps2_def_binds[i].code, in_ps2_def_binds[i].btype)] =
        1 << in_ps2_def_binds[i].bit;
}

/* remove binds of missing keys, count remaining ones */
static int in_ps2_clean_binds(void *drv_data, int *binds, int *def_binds)
{
    int i, count = 0, have_vol = 0, have_menu = 0;
    
    for (i = 0; i < IN_PS2_NBUTTONS; i++) {
        int t, eb, offs;
        for (t = 0; t < IN_BINDTYPE_COUNT; t++) {
            offs = IN_BIND_OFFS(i, t);
            if (in_ps2_keys[i] == NULL)
                binds[offs] = def_binds[offs] = 0;
            if (binds[offs])
                count++;
        }
        eb = binds[IN_BIND_OFFS(i, IN_BINDTYPE_EMU)];
//        if (eb & (PEV_VOL_DOWN|PEV_VOL_UP))
//            have_vol = 1;
//        if (eb & PEV_MENU)
//            have_menu = 1;
    }
    
    /* autobind some important keys, if they are unbound */
//    if (!have_vol && binds[BTN_VOL_UP] == 0 && binds[BTN_VOL_DOWN] == 0) {
//        binds[IN_BIND_OFFS(BTN_VOL_UP, IN_BINDTYPE_EMU)]   = PEV_VOL_UP;
//        binds[IN_BIND_OFFS(BTN_VOL_DOWN, IN_BINDTYPE_EMU)] = PEV_VOL_DOWN;
//        count += 2;
//    }
    
    if (!have_menu) {
        binds[IN_BIND_OFFS(BTN_SELECT, IN_BINDTYPE_EMU)] = PEV_MENU;
        count++;
    }
    
//    in_combos_find(binds, BTN_PUSH, &in_ps2_combo_keys, &in_ps2_combo_acts);
    
    return count;
}

void in_ps2_init(void *vdrv)
{
    in_drv_t *drv = vdrv;
//    ps2_soc_t soc;
//    
//    soc = soc_detect();
//    if (soc == SOCID_POLLUX)
//        in_ps2_keys[BTN_START] = "MENU";
    
    in_ps2_combo_keys = in_ps2_combo_acts = 0;
    
    drv->prefix = in_ps2_prefix;
    drv->probe = in_ps2_probe;
    drv->free = in_ps2_free;
    drv->get_bind_count = in_ps2_get_bind_count;
    drv->get_def_binds = in_ps2_get_def_binds;
    drv->clean_binds = in_ps2_clean_binds;
    drv->menu_translate = in_ps2_menu_translate;
    drv->get_key_code = in_ps2_get_key_code;
    drv->get_key_name = in_ps2_get_key_name;
    drv->update_keycode = in_ps2_update_keycode;
}

