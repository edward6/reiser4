/*
	journal40.h -- reiser4 default journal plugin.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#ifndef JOURNAL40_H
#define JOURNAL40_h

#include <aal/aal.h>

struct reiserfs_journal40 {
	aal_device_t *device;
	aal_block_t *header;
};

typedef struct reiserfs_journal40 reiserfs_journal40_t;

#endif

