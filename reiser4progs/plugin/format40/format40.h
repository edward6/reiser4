/*
    format40.h -- default disk-layout plugin implementation for reiserfs 4.0.
    Copyright (C) 1996 - 2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FORMAT40_H
#define FORMAT40_H

#include <aal/aal.h>

#define REISERFS_FORMAT40_MAGIC		"R4Sb-Default"
#define REISERFS_FORMAT40_OFFSET	(65536 + 4096)

#define REISERFS_JOURNAL40_HEADER	(4096 * 19)
#define REISERFS_JOURNAL40_FOOTER	(4096 * 20)

#define REISERFS_FORMAT40_JOURNAL	0x0
#define REISERFS_FORMAT40_ALLOC		0x0
#define REISERFS_FORMAT40_OID		0x0

struct reiserfs_format40_super {
    uint64_t sb_block_count;
    uint64_t sb_free_blocks;
    uint64_t sb_root_block;
    uint64_t sb_oid;
    uint64_t sb_file_count;
    uint64_t sb_flushes;
    char sb_magic[16];
    uint16_t sb_tree_height;
    
    uint16_t sb_padd[3];
    char sb_unused[420];
} __attribute__((__packed__));

typedef struct reiserfs_format40_super reiserfs_format40_super_t;

#define get_sb_block_count(sb)			get_le64(sb, sb_block_count)
#define set_sb_block_count(sb, val)		set_le64(sb, sb_block_count, val)

#define get_sb_free_blocks(sb)			get_le64(sb, sb_free_blocks)
#define set_sb_free_blocks(sb, val)		set_le64(sb, sb_free_blocks, val)

#define get_sb_root_block(sb)			get_le64(sb, sb_root_block)
#define set_sb_root_block(sb, val)		set_le64(sb, sb_root_block, val)

#define get_sb_tree_height(sb)			get_le64(sb, sb_tree_height)
#define set_sb_tree_height(sb, val)		set_le64(sb, sb_tree_height, val)

#define get_sb_oid(sb)				get_le64(sb, sb_oid)
#define set_sb_oid(sb, val)			set_le64(sb, sb_oid, val)

#define get_sb_file_count(sb)			get_le64(sb, sb_file_count)
#define set_sb_file_count(sb, val)		set_le64(sb, sb_file_count, val)

#define get_sb_flushes(sb)			get_le64(sb, sb_flushes)
#define set_sb_flushes(sb, val)			set_le64(sb, sb_flushes, val)

struct reiserfs_format40 {
    aal_device_t *device;
    aal_block_t *super;

    reiserfs_opaque_t *journal;
    reiserfs_opaque_t *alloc;
    reiserfs_opaque_t *oid;
};

typedef struct reiserfs_format40 reiserfs_format40_t;

#endif

