/*
    alloc.h -- reiserfs block allocator functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ALLOC_H
#define ALLOC_H

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

extern error_t reiserfs_alloc_open(reiserfs_fs_t *fs);
extern error_t reiserfs_alloc_create(reiserfs_fs_t *fs);
extern error_t reiserfs_alloc_sync(reiserfs_fs_t *fs);
extern void reiserfs_alloc_close(reiserfs_fs_t *fs);

extern count_t reiserfs_alloc_free(reiserfs_fs_t *fs);
extern count_t reiserfs_alloc_used(reiserfs_fs_t *fs);

#endif

