/*
	tree.c -- reiserfs balanced tree code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

int reiserfs_tree_open(reiserfs_fs_t *fs) {
	
	if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
		return 0;
	
	return 1;
}

int reiserfs_tree_create(reiserfs_fs_t *fs) {

	if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
		return 0;
	
	return 1;
}

void reiserfs_tree_close(reiserfs_fs_t *fs, int sync) {
	ASSERT(fs != NULL, return);
	ASSERT(fs->tree != NULL, return);
	
	aal_free(fs->tree);
	fs->tree = NULL;
}

