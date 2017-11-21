/* define POSIX stuff: dirent, scandir, getcwd, mkdir */
#if defined(__linux__)

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#elif defined(_EE)

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

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


