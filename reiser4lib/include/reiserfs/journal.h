/*
	journal.h -- reiserfs filesystem journal functions.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef JOURNAL_H
#define JOURNAL_H

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

extern int reiserfs_journal_open(reiserfs_fs_t *fs, aal_device_t *device, int replay);
extern int reiserfs_journal_reopen(reiserfs_fs_t *fs, aal_device_t *device, int replay);
extern void reiserfs_journal_close(reiserfs_fs_t *fs);

#endif

