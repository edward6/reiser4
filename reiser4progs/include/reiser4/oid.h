/*
    oid.h -- oid allocator functions.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OID_H
#define OID_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/filesystem.h>

extern reiserfs_oid_t *reiserfs_oid_open(reiserfs_format_t *format);
extern void reiserfs_oid_close(reiserfs_oid_t *oid);
extern errno_t reiserfs_oid_valid(reiserfs_oid_t *oid, int flags);

#ifndef ENABLE_COMPACT

extern reiserfs_oid_t *reiserfs_oid_create(reiserfs_format_t *format);

extern errno_t reiserfs_oid_sync(reiserfs_oid_t *oid);

extern uint64_t reiserfs_oid_alloc(reiserfs_oid_t *oid);
extern void reiserfs_oid_dealloc(reiserfs_oid_t *oid, uint64_t id);

#endif

extern uint64_t reiserfs_oid_free(reiserfs_oid_t *oid);
extern uint64_t reiserfs_oid_used(reiserfs_oid_t *oid);

extern oid_t reiserfs_oid_root_locality(reiserfs_oid_t *oid);
extern oid_t reiserfs_oid_root_objectid(reiserfs_oid_t *oid);
extern oid_t reiserfs_oid_root_parent_locality(reiserfs_oid_t *oid);

#endif

