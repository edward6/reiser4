/*
    object.h -- reiserfs object functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OBJECT_H
#define OBJECT_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

extern reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, 
    reiserfs_plugin_t *plugin, const char *name);

extern void reiserfs_object_close(reiserfs_object_t *object);
extern errno_t reiserfs_object_rewind(reiserfs_object_t *object);
extern uint32_t reiserfs_object_tell(reiserfs_object_t *object);

extern errno_t reiserfs_object_read(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint);

#ifndef ENABLE_COMPACT

extern reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_t *plugin, reiserfs_object_t *parent, 
    reiserfs_object_hint_t *hint, const char *name);

extern errno_t reiserfs_object_add(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint);

#endif

#endif

