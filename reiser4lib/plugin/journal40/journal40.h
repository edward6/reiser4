/*
    journal40.h -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef JOURNAL40_H
#define JOURNAL40_h

#include <aal/aal.h>

#define REISERFS_JOURNAL40_OFFSET 69632

struct reiserfs_journal40 {
    aal_device_t *device;
    aal_block_t *header;
};

typedef struct reiserfs_journal40 reiserfs_journal40_t;

struct reiserfs_journal40_header {
    /* Journal40 specific fileds must be here. */
};

typedef struct reiserfs_journal40_header reiserfs_journal40_header_t;

#endif

