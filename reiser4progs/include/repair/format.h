
/*
    repair/format.h -- reiserfs filesystem recovery structures and macros.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef REPAIR_FORMAT_H
#define REPAIR_FORMAT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/filesystem.h>

extern errno_t repair_format_check(reiserfs_fs_t *fs);
extern void repair_format_print(FILE *stream, reiserfs_fs_t *fs, uint16_t options);

#endif
