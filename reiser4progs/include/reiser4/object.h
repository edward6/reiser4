/*
    object.h -- reiserfs object functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OBJECT_H
#define OBJECT_H

extern reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, const char *name);
extern void reiserfs_object_close(reiserfs_object_t *object);

#endif

