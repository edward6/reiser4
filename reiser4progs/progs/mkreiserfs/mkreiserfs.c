#include <reiserfs/reiserfs.h>

int main () {
    reiserfs_fs_t *fs;
    
    if (0)
	fs = reiserfs_fs_open(NULL, NULL, 0);
    
    return 0;
}
