/*
    super.h -- superblock's functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef SUPER_H
#define SUPER_H

#include <aal/aal.h>
#include <reiserfs/filesystem.h>

extern error_t reiserfs_super_open(reiserfs_fs_t *fs);
extern error_t reiserfs_super_reopen(reiserfs_fs_t *fs);
extern void reiserfs_super_close(reiserfs_fs_t *fs);
extern error_t reiserfs_super_sync(reiserfs_fs_t *fs);

extern error_t reiserfs_super_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_id_t plugin_id, count_t len);

extern const char *reiserfs_super_format(reiserfs_fs_t *fs);

extern blk_t reiserfs_super_offset(reiserfs_fs_t *fs);

extern blk_t reiserfs_super_get_root(reiserfs_fs_t *fs);
extern count_t reiserfs_super_get_blocks(reiserfs_fs_t *fs);
extern count_t reiserfs_super_get_free(reiserfs_fs_t *fs);

extern void reiserfs_super_set_root(reiserfs_fs_t *fs, blk_t root);
extern void reiserfs_super_set_blocks(reiserfs_fs_t *fs, count_t blocks);
extern void reiserfs_super_set_free(reiserfs_fs_t *fs, count_t blocks);

extern reiserfs_plugin_id_t reiserfs_super_journal_plugin_id(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_super_alloc_plugin_id(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_super_node_plugin_id(reiserfs_fs_t *fs);

#endif

