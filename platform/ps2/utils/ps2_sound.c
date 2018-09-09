#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <audsrv.h>

#include "ps2_config.h"
#include "ps2_pico.h"
#include "ps2_sound.h"
#include "ps2_timing.h"
#include "../mp3.h"
#include "../plat_ps2.h"

#include <pico/pico_int.h>
#include <pico/cd/cue.h>
#include "../../common/emu.h"

// TODO: THIS CLASS NEED A REFACTOR
// Extract every use of the config for Pico or sound different class
// Make easier to read where the pointers/constants used for the audio
// Remove the values and constants in the file for something with more sense.
