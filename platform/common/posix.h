/* define POSIX stuff: dirent, scandir, getcwd, mkdir */
#if defined(__linux__) || defined(__MINGW32__)

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __MINGW32__
#warning hacks!
#define mkdir(pathname,mode) mkdir(pathname)
#define d_type d_ino
#define DT_REG 0
#define DT_DIR 0
#endif

#elif defined(_EE)

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

//Imports needed because common classes uses this methdods
#include <fileXio_rpc.h>
#include "../ps2/utils/io_suppliment.h"
#include "../ps2/utils/ps2_modules.h"
#include "../ps2/in_ps2.h"

#define APA_FLAG_SUB        0x0001
#define DT_DIR 4607
#define DT_REG 8703

//Create those function that are not included in the PS2SDK

#define rename(old_file, new_file) fileXioRename(old_file, new_file)
#define remove(file) ps2_remove(file)
#define mkdir(filename, mode) ps2_mkdir(filename, mode)
#define alphasort(dirent1, dirent2) (strcmp((*dirent1)->d_name, (*dirent2)->d_name))

#else

#error "must provide posix"

#endif


