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

struct oid40 {
    oid_t used;
    oid_t next;
};

typedef struct oid40 oid40_t;

#endif

