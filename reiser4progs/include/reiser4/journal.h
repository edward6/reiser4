/*
    journal.h -- reiser4 filesystem journal functions.
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

extern reiser4_journal_t *reiser4_journal_open(reiser4_format_t *format,
    aal_device_t *device);

#ifndef ENABLE_COMPACT

extern reiser4_journal_t *reiser4_journal_create(reiser4_format_t *format,
    aal_device_t *device, void *params);

extern errno_t reiser4_journal_sync(reiser4_journal_t *journal);
extern errno_t reiser4_journal_replay(reiser4_journal_t *journal);

#endif

extern errno_t reiser4_journal_valid(reiser4_journal_t *journal, 
    int flags);

extern void reiser4_journal_close(reiser4_journal_t *journal);

#endif

