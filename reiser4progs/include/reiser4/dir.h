/*
    dir.h -- functions which work with directory.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR_H
#define DIR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

extern reiser4_object_t *reiser4_dir_open(reiser4_fs_t *fs, 
    const char *name);

extern void reiser4_dir_close(reiser4_object_t *object);

#ifndef ENABLE_COMPACT

extern reiser4_object_t *reiser4_dir_create(reiser4_fs_t *fs,
    reiser4_object_hint_t *hint, reiser4_plugin_t *plugin,
    reiser4_object_t *parent, const char *name);

extern errno_t reiser4_dir_add(reiser4_object_t *object, 
    reiser4_entry_hint_t *hint);

#endif

extern errno_t reiser4_dir_rewind(reiser4_object_t *object);

extern errno_t reiser4_dir_read(reiser4_object_t *object, 
    reiser4_entry_hint_t *hint);

extern uint32_t reiser4_dir_tell(reiser4_object_t *object);

extern int32_t reiser4_dir_seek(reiser4_object_t *object, 
    uint32_t pos);

#endif
