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

extern reiserfs_object_t *reiserfs_dir_open(reiserfs_fs_t *fs, 
    const char *name);

extern void reiserfs_dir_close(reiserfs_object_t *object);

#ifndef ENABLE_COMPACT

extern reiserfs_object_t *reiserfs_dir_create(reiserfs_fs_t *fs,
    reiserfs_object_hint_t *hint, reiserfs_plugin_t *plugin,
    reiserfs_object_t *parent, const char *name);

extern errno_t reiserfs_dir_add(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint);

#endif

extern errno_t reiserfs_dir_rewind(reiserfs_object_t *object);

extern errno_t reiserfs_dir_read(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint);

extern uint32_t reiserfs_dir_tell(reiserfs_object_t *object);

#endif
