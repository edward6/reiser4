/*
	tree.h -- reiserfs balanced tree functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef TREE_H
#define TREE_H

#include <reiserfs/reiserfs.h>

extern int reiserfs_tree_open(reiserfs_fs_t *fs);
extern void reiserfs_tree_close(reiserfs_fs_t *fs);

#endif

