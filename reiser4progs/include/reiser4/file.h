/*
    object.h -- reiser4 object functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OBJECT_H
#define OBJECT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

extern reiser4_file_t *reiser4_file_open(reiser4_fs_t *fs, 
    const char *name);

extern void reiser4_file_close(reiser4_file_t *object);
extern reiser4_plugin_t *reiser4_file_guess(reiser4_file_t *object);

extern errno_t reiser4_file_read(reiser4_file_t *file,
    char *buff, uint64_t n);

#ifndef ENABLE_COMPACT

extern reiser4_file_t *reiser4_file_create(reiser4_fs_t *fs,
    reiser4_file_hint_t *hint, reiser4_file_t *parent, 
    const char *name);

extern errno_t reiser4_file_write(reiser4_file_t *object,
    char *buff, uint64_t n);

extern errno_t reiser4_file_truncate(reiser4_file_t *object,
    uint64_t n);

#endif

extern errno_t reiser4_file_reset(reiser4_file_t *object);
extern uint32_t reiser4_file_offset(reiser4_file_t *object);

extern errno_t reiser4_file_seek(reiser4_file_t *object,
    uint32_t offset);

#endif

