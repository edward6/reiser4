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

extern errno_t repair_format_check(reiser4_fs_t *fs);
extern void repair_format_print(reiser4_fs_t *fs, FILE *stream, 
    uint16_t options);
extern errno_t callback_data_block_check(reiser4_entity_t *format, blk_t blk, 
    void *data);
#endif
