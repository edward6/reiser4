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

extern reiser4_object_t *reiser4_object_open(reiser4_fs_t *fs, 
    const char *name);

extern void reiser4_object_close(reiser4_object_t *object);
extern reiser4_plugin_t *reiser4_object_guess(reiser4_object_t *object);

#ifndef ENABLE_COMPACT
extern reiser4_object_t *reiser4_object_create(reiser4_fs_t *fs);
#endif

#endif

