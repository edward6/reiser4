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

extern reiserfs_journal_t *reiserfs_journal_open(aal_device_t *device,
    reiserfs_id_t plugin_id);

#ifndef ENABLE_COMPACT

extern reiserfs_journal_t *reiserfs_journal_create(aal_device_t *device,
    reiserfs_opaque_t *params, reiserfs_id_t plugin_id);

extern error_t reiserfs_journal_sync(reiserfs_journal_t *journal);
extern error_t reiserfs_journal_replay(reiserfs_journal_t *journal);

#endif

extern void reiserfs_journal_close(reiserfs_journal_t *journal);

#endif

