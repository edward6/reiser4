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

#endif

