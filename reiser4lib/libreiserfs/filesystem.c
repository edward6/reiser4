/*
	filesystem.c -- common reiserfs filesystem code.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

/* Opens the filesystem and journal on the specified devices. */
reiserfs_fs_t *reiserfs_fs_open(device_t *host_device, device_t *journal_device) {
	ASSERT(host_device != NULL, return NULL);
	ASSERT(journal_device != NULL, return NULL);
}

/* Closes filesystem. Closes all filesystem's entities. Frees all assosiated memory. */
void reiserfs_fs_close(reiserfs_fs_t *fs) {
	reiserfs_journal_close(fs);
	reiserfs_alloc_close(fs);
	reiserfs_super_close(fs);
	reiserfs_tree_close(fs);
	libreiserfs_free(fs);
}

