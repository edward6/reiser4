/*
    dir.h -- reiserfs directory functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef DIR_H
#define DIR_H

#include <reiser4/filesystem.h>

extern reiserfs_dir_t *reiserfs_dir_create(reiserfs_dir_t *parent, 
    reiserfs_dir_info_t *info);

extern reiserfs_dir_t *reiserfs_dir_init(void);
extern void reiserfs_dir_fini(reiserfs_dir_t *dir);

#endif

