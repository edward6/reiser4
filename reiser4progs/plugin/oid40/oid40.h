/*
    oid40.h -- reiser4 default oid allocator structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OID40_H
#define OID40_H

#include <aal/aal.h>

#define REISERFS_OID40_ROOT_PARENT_LOCALITY	0x26
#define REISERFS_OID40_ROOT_PARENT_OBJECTID	0x29

#define REISERFS_OID40_ROOT_OBJECTID		0x2a
#define REISERFS_OID40_RESERVED			(1 << 16)

struct reiserfs_oid40 {
    void *area;
    uint32_t len;
};

typedef struct reiserfs_oid40 reiserfs_oid40_t;

#define oid40_get_next(area)			LE64_TO_CPU(*((uint64_t *)area))
#define oid40_set_next(area, val)		(*((uint64_t *)area) = CPU_TO_LE64(val))

#define oid40_get_used(area)			LE64_TO_CPU(*(((uint64_t *)area) + 1))
#define oid40_set_used(area, val)		(*(((uint64_t *)area) + 1) = CPU_TO_LE64(val))

#endif

