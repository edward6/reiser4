/*
    journal.h -- reiserfs filesystem journal functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL_H
#define JOURNAL_H

#include <aal/aal.h>
#include <reiser4/filesystem.h>
#include <reiser4/plugin.h>

extern error_t reiserfs_journal_open(reiserfs_fs_t *fs, 
    int replay);

extern error_t reiserfs_journal_sync(reiserfs_fs_t *fs);
extern void reiserfs_journal_close(reiserfs_fs_t *fs);

#endif

