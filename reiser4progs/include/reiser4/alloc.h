/*
    alloc.h -- reiserfs block allocator functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC_H
#define ALLOC_H

#include <aal/aal.h>
#include <reiser4/filesystem.h>
#include <reiser4/plugin.h>

extern error_t reiserfs_alloc_open(reiserfs_fs_t *fs);

extern error_t reiserfs_alloc_sync(reiserfs_fs_t *fs);
extern void reiserfs_alloc_close(reiserfs_fs_t *fs);

extern count_t reiserfs_alloc_free(reiserfs_fs_t *fs);
extern count_t reiserfs_alloc_used(reiserfs_fs_t *fs);

extern void reiserfs_alloc_mark(reiserfs_fs_t *fs, blk_t blk);
extern int reiserfs_alloc_test(reiserfs_fs_t *fs, blk_t blk);

extern void reiserfs_alloc_dealloc(reiserfs_fs_t *fs, blk_t blk);
extern blk_t reiserfs_alloc_alloc(reiserfs_fs_t *fs);

#endif

