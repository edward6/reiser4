/*
	format36.h -- disk-layout plugin for reiserfs 3.6.x
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef FORMAT36_H
#define FORMAT36_H

#include <aal/aal.h>

#define REISERFS_3_5_SUPER_SIGNATURE "ReIsErFs"
#define REISERFS_3_6_SUPER_SIGNATURE "ReIsEr2Fs"
#define REISERFS_JR_SUPER_SIGNATURE "ReIsEr3Fs"

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

struct reiserfs_format36_super {
    reiserfs_super_v1_t s_v1;
    uint32_t s_inode_generation;
    uint32_t s_flags;
    char s_uuid[16];
    char s_label[16];
    char s_unused[88];
};

typedef struct reiserfs_format36_super reiserfs_format36_super_t;

struct reiserfs_format36 {
    aal_device_t *device;
    aal_block_t *super;
};

typedef struct reiserfs_format36 reiserfs_format36_t;

#define SUPER_V1_SIZE				(sizeof(reiserfs_super_v1_t))
#define SUPER_V2_SIZE				(sizeof(reiserfs_super_t))

#define get_sb_jp(sb)				(&((sb)->s_v1.sb_journal))

#define get_sb_block_count(sb)			get_le32(sb, s_v1.sb_block_count)
#define set_sb_block_count(sb, val)		set_le32(sb, s_v1.sb_block_count, val)

#define get_sb_free_blocks(sb) 			get_le32(sb, s_v1.sb_free_blocks)
#define set_sb_free_blocks(sb, val)		set_le32(sb, s_v1.sb_free_blocks, val)

#define get_sb_root_block(sb)			get_le32(sb, s_v1.sb_root_block)
#define set_sb_root_block(sb, val)		set_le32(sb, s_v1.sb_root_block, val)

#define get_sb_mount_id(sb)			get_le32(sb, s_v1.sb_mountid)
#define set_sb_mount_id(sb, val)		set_le32(sb, s_v1.sb_mountid, val)

#define get_sb_block_size(sb)			get_le16(sb, s_v1.sb_block_size)
#define set_sb_block_size(sb, val)		set_le16(sb, s_v1.sb_block_size, val)

#define get_sb_oid_maxsize(sb)			get_le16(sb, s_v1.sb_oid_maxsize)
#define set_sb_oid_maxsize(sb, val)		set_le16(sb, s_v1.sb_oid_maxsize, val)

#define get_sb_oid_cursize(sb)			get_le16(sb, s_v1.sb_oid_cursize)
#define set_sb_oid_cursize(sb, val)		set_le16(sb, s_v1.sb_oid_cursize, val)

#define get_sb_umount_state(sb)			get_le16(sb, s_v1.sb_umount_state)
#define set_sb_umount_state(sb, val)		set_le16(sb, s_v1.sb_umount_state, val)

#define get_sb_fs_state(sb)			get_le16(sb, s_v1.sb_fs_state)
#define set_sb_fs_state(sb, val)		set_le16(sb, s_v1.sb_fs_state, val)

#define get_sb_hash_code(sb) 			get_le32(sb, s_v1.sb_hash_function_code)
#define set_sb_hash_code(sb, val)		set_le32(sb, s_v1.sb_hash_function_code, val)

#define get_sb_tree_height(sb)			get_le16(sb, s_v1.sb_tree_height)
#define set_sb_tree_height(sb, val)		set_le16(sb, s_v1.sb_tree_height, val)

#define get_sb_bmap_nr(sb)			get_le16(sb, s_v1.sb_bmap_nr)
#define set_sb_bmap_nr(sb, val)			set_le16(sb, s_v1.sb_bmap_nr, val)

#define get_sb_format(sb)			get_le16(sb, s_v1.sb_format)
#define set_sb_format(sb, val)			set_le16(sb, s_v1.sb_format, val)

#define get_sb_reserved_for_journal(sb)		get_le16(sb, s_v1.sb_reserved_for_journal)
#define set_sb_reserved_for_journal(sb,val)	set_le16(sb, s_v1.sb_reserved_for_journal,val)

#define get_jp_start(jp)			get_le32(jp, jp_start)
#define set_jp_start(jp, val)			set_le32(jp, jp_start, val)

#define get_jp_dev(jp)				get_le32(jp, jp_dev)
#define set_jp_dev(jp, val)			set_le32(jp, jp_dev, val)

#define get_jp_len(jp)				get_le32(jp, jp_len)
#define set_jp_len(jp, val)			set_le32(jp, jp_len, val)

#define get_jp_max_trans_len(jp)		get_le32(jp, jp_trans_max)
#define set_jp_max_trans_len(jp, val)		set_le32(jp, jp_trans_max, val)

#define get_jp_magic(jp)			get_le32(jp, jp_magic)
#define set_jp_magic(jp,val)			set_le32(jp, jp_magic, val)

#define get_jp_max_batch(jp)			get_le32(jp, jp_max_batch)
#define set_jp_max_batch(jp,val)		set_le32(jp, jp_max_batch, val)

#define get_jp_max_commit_age(jp)		get_le32(jp, jp_max_commit_age)
#define set_jp_max_commit_age(jp, val)		set_le32(jp, jp_max_commit_age, val)

#define get_jp_max_trans_age(jp)		get_le32(jp, jp_max_trans_age)
#define set_jp_max_trans_age(jp,val)		set_le32(jp, jp_max_trans_age, val)
	
#endif

