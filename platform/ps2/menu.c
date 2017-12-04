// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

// don't like to use loads of #ifdefs, so duplicating GP2X code
// horribly instead

#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <unistd.h>
#include <limits.h>
#include <sys/fcntl.h>

#include <kernel.h>
#include <fileXio_rpc.h>
#include <gsKit.h>
#include <libpad.h>

#include "../common/posix.h"
#include "../common/emu.h"
#include "../common/menu.h"
#include "../common/readpng.h"
#include "../common/input.h"

#include <pico/pico_int.h>
#include <pico/patch.h>
#include <zlib/zlib.h>

