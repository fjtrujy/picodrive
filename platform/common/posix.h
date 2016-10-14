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

struct my_dirent
{
    unsigned int d_type;
    char d_name[255];
};

//Create those function that are not included in the PS2SDK

#define rename(old_file, new_file) fileXioRename(old_file, new_file)
#define remove(file) ps2_remove(file)
#define mkdir(filename, mode) ps2_mkdir(filename, mode)
#define alphasort(dirent1, dirent2) (strcmp((*dirent1)->d_name, (*dirent2)->d_name))

#else

#error "must provide posix"

#endif


