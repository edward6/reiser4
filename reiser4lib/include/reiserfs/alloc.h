/*
	alloc.h -- reiserfs block allocator functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef ALLOC_H
#define ALLOC_H

#include <reiserfs/reiserfs.h>

extern int reiserfs_alloc_open(reiserfs_fs_t *fs);
extern void reiserfs_alloc_close(reiserfs_fs_t *fs);

#endif

