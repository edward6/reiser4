/*
    progs/filesystem.h -- reiserfs filesystem recovery structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include <progs/progs.h>

extern errno_t progs_fs_check(reiserfs_fs_t *fs);
