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

extern reiser4_oid_t *reiser4_oid_open(reiser4_format_t *format);
extern void reiser4_oid_close(reiser4_oid_t *oid);
extern errno_t reiser4_oid_valid(reiser4_oid_t *oid);

#ifndef ENABLE_COMPACT

extern reiser4_oid_t *reiser4_oid_create(reiser4_format_t *format);
extern errno_t reiser4_oid_sync(reiser4_oid_t *oid);

extern roid_t reiser4_oid_next(reiser4_oid_t *oid);
extern roid_t reiser4_oid_allocate(reiser4_oid_t *oid);
extern void reiser4_oid_release(reiser4_oid_t *oid, roid_t id);

#endif

extern uint64_t reiser4_oid_free(reiser4_oid_t *oid);
extern uint64_t reiser4_oid_used(reiser4_oid_t *oid);

extern roid_t reiser4_oid_root_locality(reiser4_oid_t *oid);
extern roid_t reiser4_oid_root_objectid(reiser4_oid_t *oid);
extern roid_t reiser4_oid_root_parent_locality(reiser4_oid_t *oid);

#endif

