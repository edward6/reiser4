/*
	layout36.h -- disk-layout plugin for reiserfs 3.6.x
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#ifndef LAYOUT36_H
#define LAYOUT36_H

#include <stdint.h>
#include <aal/aal.h>

struct reiserfs_journal_params {
	uint32_t jp_start;
	uint32_t jp_dev;
	uint32_t jp_len;
	uint32_t jp_trans_max;
	uint32_t jp_magic;
	uint32_t jp_max_batch;
	uint32_t jp_max_commit_age;
	uint32_t jp_max_trans_age;
};

typedef struct reiserfs_journal_params reiserfs_journal_params_t;

struct reiserfs_super_v1 {
	uint32_t sb_block_count;
	uint32_t sb_free_blocks;
	uint32_t sb_root_block;
	reiserfs_journal_params_t sb_journal;
	uint16_t sb_block_size;
	uint16_t sb_oid_maxsize;
	uint16_t sb_oid_cursize;
	uint16_t sb_umount_state;
	char sb_magic[10];
	uint16_t sb_fs_state;
	uint32_t sb_hash_function_code;
	int16_t sb_tree_height;
	uint16_t sb_bmap_nr;
	uint16_t sb_format;
	uint16_t sb_reserved_for_journal;
} __attribute__ ((__packed__));

typedef struct reiserfs_super_v1 reiserfs_super_v1_t;

struct reiserfs_layout36_super {
	reiserfs_super_v1_t s_v1;
	uint32_t s_inode_generation;
	uint32_t s_flags;
	char s_uuid[16];
	char s_label[16];
	char s_unused[88];
};

typedef struct reiserfs_layout36_super reiserfs_layout36_super_t;

struct reiserfs_layout36 {
	aal_device_t *device;
};

typedef struct reiserfs_layout36 reiserfs_layout36_t;

#endif

