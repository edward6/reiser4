/*
    format.h -- format's functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef FORMAT_H
#define FORMAT_H

#include <aal/aal.h>
#include <reiser4/plugin.h>
#include <reiser4/filesystem.h>

extern error_t reiserfs_format_init(reiserfs_fs_t *fs);
extern error_t reiserfs_format_reinit(reiserfs_fs_t *fs);
extern void reiserfs_format_fini(reiserfs_fs_t *fs);
extern error_t reiserfs_format_sync(reiserfs_fs_t *fs);

extern error_t reiserfs_format_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_id_t format_plugin_id, count_t len, 
    reiserfs_params_opaque_t *journal_params);

extern const char *reiserfs_format_format(reiserfs_fs_t *fs);

extern blk_t reiserfs_format_offset(reiserfs_fs_t *fs);

extern blk_t reiserfs_format_get_root(reiserfs_fs_t *fs);
extern count_t reiserfs_format_get_blocks(reiserfs_fs_t *fs);
extern count_t reiserfs_format_get_free(reiserfs_fs_t *fs);
extern uint16_t reiserfs_format_get_height(reiserfs_fs_t *fs);

extern void reiserfs_format_set_root(reiserfs_fs_t *fs, blk_t root);
extern void reiserfs_format_set_blocks(reiserfs_fs_t *fs, count_t blocks);
extern void reiserfs_format_set_free(reiserfs_fs_t *fs, count_t blocks);
extern void reiserfs_format_set_height(reiserfs_fs_t *fs, uint16_t height);

extern reiserfs_plugin_id_t reiserfs_format_journal_plugin_id(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_format_alloc_plugin_id(reiserfs_fs_t *fs);
extern reiserfs_plugin_id_t reiserfs_format_oid_plugin_id(reiserfs_fs_t *fs);

extern reiserfs_opaque_t *reiserfs_format_journal(reiserfs_fs_t *fs);
extern reiserfs_opaque_t *reiserfs_format_alloc(reiserfs_fs_t *fs);
extern reiserfs_opaque_t *reiserfs_format_oid(reiserfs_fs_t *fs);

#endif

