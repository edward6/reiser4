/*
	super.h -- superblock's functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef SUPER_H
#define SUPER_H

#include <reiserfs/filesystem.h>

extern int reiserfs_super_open(reiserfs_fs_t *fs);
extern void reiserfs_super_close(reiserfs_fs_t *fs);

#endif

