/*
    oid.h -- oid allocator functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OID_H
#define OID_H

#include <aal/aal.h>
#include <reiserfs/filesystem.h>

extern error_t reiserfs_oid_init(reiserfs_fs_t *fs);
extern void reiserfs_oid_close(reiserfs_fs_t *fs);

extern uint64_t reiserfs_oid_alloc(reiserfs_fs_t *fs);
extern void reiserfs_oid_dealloc(reiserfs_fs_t *fs, uint64_t oid);

extern uint64_t reiserfs_oid_next(reiserfs_fs_t *fs);
extern uint64_t reiserfs_oid_used(reiserfs_fs_t *fs);

extern uint64_t reiserfs_oid_root_parent_locality(reiserfs_fs_t *fs);
extern uint64_t reiserfs_oid_root_self_locality(reiserfs_fs_t *fs);
extern uint64_t reiserfs_oid_root(reiserfs_fs_t *fs);

#endif

