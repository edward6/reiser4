/*
	journal36.h -- journal plugin for reiserfs 3.6.x.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef JOURNAL36_H
#define JOURNAL36_h

#include <aal/aal.h>

struct reiserfs_journal36 {
    aal_device_t *device;
    aal_block_t *header;
};

typedef struct reiserfs_journal36 reiserfs_journal36_t;

struct reiserfs_journal36_header {
    char jh_unused[100];
};

typedef struct reiserfs_journal36_header reiserfs_journal36_header_t;

#endif

