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

#ifndef ENABLE_COMPACT

extern reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_t *plugin, reiserfs_object_t *parent, const char *name);

#endif

#endif

