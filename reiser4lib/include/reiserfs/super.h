/*
	super.h -- superblock's functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef SUPER_H
#define SUPER_H

#include <reiserfs/filesystem.h>

extern int reiserfs_super_open(reiserfs_fs_t *fs);
extern void reiserfs_super_close(reiserfs_fs_t *fs, int sync);

extern int reiserfs_super_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t format, 
	unsigned int blocksize, const char *uuid, const char *label, count_t len);

extern const char *reiserfs_super_format(reiserfs_fs_t *fs);
extern unsigned int reiserfs_super_blocksize(reiserfs_fs_t *fs);

extern reiserfs_plugin_id_t reiserfs_super_journal_plugin(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_super_alloc_plugin(reiserfs_fs_t *fs);

#endif

