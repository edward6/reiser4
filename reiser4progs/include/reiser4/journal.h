/*
    journal.h -- reiserfs filesystem journal functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL_H
#define JOURNAL_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/filesystem.h>
#include <reiser4/plugin.h>

extern reiserfs_journal_t *reiserfs_journal_open(reiserfs_format_t *format,
    aal_device_t *device);

#ifndef ENABLE_COMPACT

extern reiserfs_journal_t *reiserfs_journal_create(reiserfs_format_t *format,
    aal_device_t *device, void *params);

extern errno_t reiserfs_journal_sync(reiserfs_journal_t *journal);
extern errno_t reiserfs_journal_replay(reiserfs_journal_t *journal);

#endif

extern errno_t reiserfs_journal_valid(reiserfs_journal_t *journal, 
    int flags);

extern void reiserfs_journal_close(reiserfs_journal_t *journal);

#endif

