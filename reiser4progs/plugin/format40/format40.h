/*
    format40.h -- default disk-layout plugin implementation for reiser4.
    Copyright (C) 1996 - 2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FORMAT40_H
#define FORMAT40_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/plugin.h>

#define FORMAT40_MAGIC	    "R4Sb-Default"
#define FORMAT40_OFFSET	    (65536 + 4096)

struct format40_super {
    d64_t sb_block_count;
    d64_t sb_free_blocks;
    d64_t sb_root_block;
    d64_t sb_oid;
    d64_t sb_file_count;
    d64_t sb_flushes;
    d32_t sb_mkfs_id;
    char sb_magic[16];

    d16_t sb_tree_height;
    d16_t sb_tail_policy;

    char sb_unused[440];
};

typedef struct format40_super format40_super_t;

#define format40_super(block) ((format40_super_t *)block->data)

#define get_sb_mkfs_id(sb)			aal_get_le32(sb, sb_mkfs_id)
#define set_sb_mkfs_id(sb, val)			aal_set_le32(sb, sb_mkfs_id, val)

#define get_sb_block_count(sb)			aal_get_le64(sb, sb_block_count)
#define set_sb_block_count(sb, val)		aal_set_le64(sb, sb_block_count, val)

#define get_sb_free_blocks(sb)			aal_get_le64(sb, sb_free_blocks)
#define set_sb_free_blocks(sb, val)		aal_set_le64(sb, sb_free_blocks, val)

#define get_sb_root_block(sb)			aal_get_le64(sb, sb_root_block)
#define set_sb_root_block(sb, val)		aal_set_le64(sb, sb_root_block, val)

#define get_sb_tail_policy(sb)			aal_get_le16(sb, sb_tail_policy)
#define set_sb_tail_policy(sb, val)		aal_set_le16(sb, sb_tail_policy, val)

#define get_sb_oid(sb)				aal_get_le64(sb, sb_oid)
#define set_sb_oid(sb, val)			aal_set_le64(sb, sb_oid, val)

#define get_sb_file_count(sb)			aal_get_le64(sb, sb_file_count)
#define set_sb_file_count(sb, val)		aal_set_le64(sb, sb_file_count, val)

#define get_sb_flushes(sb)			aal_get_le64(sb, sb_flushes)
#define set_sb_flushes(sb, val)			aal_set_le64(sb, sb_flushes, val)

#define get_sb_tree_height(sb)			aal_get_le16(sb, sb_tree_height)
#define set_sb_tree_height(sb, val)		aal_set_le16(sb, sb_tree_height, val)

struct format40 {
    reiser4_plugin_t *plugin;

    aal_block_t *block;
    aal_device_t *device;
};

typedef struct format40 format40_t;

#endif
