/*
    repair/filesystem.h -- reiserfs filesystem recovery structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef REPAIR_FILESYSTEM_H
#define REPAIR_FILESYSTEM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <repair/repair.h>
#include <reiser4/filesystem.h>

typedef uint16_t(*callback_ask_user_t)(reiser4_fs_t *fs, int *error);

extern reiser4_fs_t *repair_fs_open(repair_data_t *data, callback_ask_user_t ask_blocksize);
extern errno_t repair_fs_sync(reiser4_fs_t *fs);
extern void repair_fs_close(reiser4_fs_t *fs);
extern errno_t repair_fs_check(reiser4_fs_t *fs);

#endif
