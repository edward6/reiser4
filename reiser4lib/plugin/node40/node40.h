/*
    node40.h -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser <reiser@namesys.com>.
    Author: Vitaly Fertman <vitaly@namesys.com>.
*/

#ifndef NODE40_H
#define NODE40_h

#include <aal/aal.h>

typedef struct node_header_40 {
    
}

struct reiserfs_node40 {
	aal_device_t *block;
	aal_block_t *header;
};

typedef struct reiserfs_journal40 reiserfs_journal40_t;

struct reiserfs_journal40_header {
	/* Journal40 specific fileds must be here. */
};

typedef struct reiserfs_journal40_header reiserfs_journal40_header_t;

#endif

