/*
	layout40.h -- default disk-layout plugin implementation for reiserfs 4.0
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef LAYOUT40_H
#define LAYOUT40_H

#include <stdint.h>
#include <aal/aal.h>

struct reiserfs_layout40_super {
	uint64_t s_block_count;
	uint64_t s_free_blocks;
	uint64_t s_root_block;
	uint16_t s_tree_height;
	uint16_t s_padd[3];
	uint64_t s_oid;
	uint64_t s_file_count;
	uint64_t s_flushes;
	char s_unused[424];
};

typedef struct reiserfs_layout40_super reiserfs_layout40_super_t;

struct reiserfs_layout40 {
	aal_device_t *device;
};

typedef struct reiserfs_layout40 reiserfs_layout40_t;

#endif

