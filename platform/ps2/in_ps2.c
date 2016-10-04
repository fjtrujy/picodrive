//
//  in_ps2.c
//  PicoDrive
//
//  Created by Francisco Javier Trujillo Mata on 03/10/16.
//  Copyright © 2016 Francisco Trujillo. All rights reserved.
//

#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "../common/input.h"
#include "in_ps2.h"

#define IN_PREFIX "ps2:"
#define IN_PS2_NBUTTONS 32

static int (*in_ps2_get_bits)(void);

enum  { BTN_UP = 4,      BTN_LEFT = 7,      BTN_DOWN = 6,  BTN_RIGHT = 5,
    BTN_START = 3,   BTN_SELECT = 0,    BTN_L1 = 10,    BTN_R1 = 11,
    BTN_X = 14,      BTN_SQUARE = 15,        BTN_CIRCLE = 13,    BTN_TRIANGLE = 12,
    BTN_L2 = 8,     BTN_R2 = 9,       BTN_L3 = 1,            BTN_R3 = 2
    };

static const char * const in_ps2_prefix = IN_PREFIX;
static const char * const in_ps2_keys[IN_PS2_NBUTTONS] = {
    [0 ... IN_PS2_NBUTTONS-1] = NULL,
    [BTN_UP]    = "UP",    [BTN_LEFT]   = "LEFT",   [BTN_DOWN] = "DOWN", [BTN_RIGHT] = "RIGHT",
    [BTN_START] = "START", [BTN_SELECT] = "SELECT", [BTN_L1]    = "L",    [BTN_R1]     = "R",
    [BTN_X]     = "X",     [BTN_SQUARE] = "SQUARE",      [BTN_CIRCLE] = "CIRCLE",    [BTN_TRIANGLE] = "TRIANGLE"
};

static void in_ps2_probe(void)
{
    in_register(IN_PREFIX "PS2 pad", IN_DRVID_PS2, -1, (void *)1);
}

static int in_ps2_get_bind_count(void)
{
    return IN_PS2_NBUTTONS;
}

static int in_ps2_get_gpio_bits(void)
{
    //extern int current_keys;
    //return current_keys;
    int keycode = ps2_pad_read_all();
    
    switch (keycode) {
        case PAD_UP:	return BTN_UP;
        case PAD_LEFT:	return BTN_LEFT;
        case PAD_DOWN:	return BTN_DOWN;
        case PAD_RIGHT:	return BTN_RIGHT;
        case PAD_CIRCLE: return BTN_CIRCLE;
        case PAD_CROSS:	return BTN_X;
        case PAD_START:	return BTN_START;
        default:	return -1;
    }
}

/* returns bitfield of binds of pressed buttons */
int in_ps2_update(void *drv_data, int *binds)
{
    int i, value, ret = 0;
    
    value = in_ps2_get_gpio_bits();
    
    return value;
    //TODO COMBOS
    /*
     for (i = 0; value; i++) {
     if (value & 1)
     ret |= binds[i];
     value >>= 1;
     }
     
     return ret;
     */
}

int in_ps2_update_keycode(void *data, int *is_down)
{
    static int old_val = 0;
    int val, diff, i;
    
    val = in_ps2_get_gpio_bits();
    
    if (old_val != val) {
        old_val = val;
        *is_down = 1;
    } else {
        *is_down = 0;
    }
    
    return val;
    
    //TODO COMBOS
    /*
     diff = val ^ old_val;
     if (diff == 0) {
     lprintf("Este es el puto problema\n");
     return -1;
     }
     
     
     // take one bit only
     for (i = 0; i < sizeof(diff)*8; i++)
     if (diff & (1<<i))
     break;
     
     old_val ^= 1 << i;
     
     if (is_down)
     *is_down = !!(val & (1<<i));
     return i;
     */
}

static int in_ps2_menu_translate(int keycode)
{
    switch (keycode) {
        case BTN_UP:	return PBTN_UP;
        case BTN_LEFT:	return PBTN_LEFT;
        case BTN_DOWN:	return PBTN_DOWN;
        case BTN_RIGHT:	return PBTN_RIGHT;
        case BTN_CIRCLE: return PBTN_MOK;
        case BTN_X:	return PBTN_MBACK;
        case BTN_START:	return PBTN_MENU;
        default:	return 0;
    }
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
    short bit;
} in_ps2_def_binds[] =
{
    /* MXYZ SACB RLDU */
    { BTN_UP,	0 },
    { BTN_DOWN,	1 },
    { BTN_LEFT,	2 },
    { BTN_RIGHT,	3 },
    { BTN_X,	4 },	/* B */
    { BTN_SQUARE,	5 },	/* C */
    { BTN_CIRCLE,	6 },	/* A */
    { BTN_START,	7 },
};

#define DEF_BIND_COUNT (sizeof(in_ps2_def_binds) / sizeof(in_ps2_def_binds[0]))

static void in_ps2_get_def_binds(int *binds)
{
    int i;
    
    for (i = 0; i < DEF_BIND_COUNT; i++)
        binds[in_ps2_def_binds[i].code] = 1 << in_ps2_def_binds[i].bit;
}

/* remove binds of missing keys, count remaining ones */
static int in_ps2_clean_binds(void *drv_data, int *binds)
{
    int i, count = 0;
    
    for (i = 0; i < IN_PS2_NBUTTONS; i++) {
        if (in_ps2_keys[i] == NULL)
            binds[i] = binds[i + IN_PS2_NBUTTONS] = 0;
        if (binds[i])
            count++;
    }
    
    return count;
    
}
void in_ps2_init(void *vdrv)
{
    in_drv_t *drv = vdrv;
    
    drv->prefix = in_ps2_prefix;
    drv->probe = in_ps2_probe;
    drv->get_bind_count = in_ps2_get_bind_count;
    drv->get_def_binds = in_ps2_get_def_binds;
    drv->clean_binds = in_ps2_clean_binds;
    drv->menu_translate = in_ps2_menu_translate;
    drv->get_key_code = in_ps2_get_key_code;
    drv->get_key_name = in_ps2_get_key_name;
    drv->update_keycode = in_ps2_update_keycode;
}

