/*
    oid40.h -- reiser4 default oid allocator structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef OID40_H
#define OID40_H

#include <aal/aal.h>

#define REISERFS_OID40_RESERVED (1 << 16)

struct reiserfs_oid40 {
    aal_block_t *block;
    uint16_t offset;
    uint64_t next;
};

typedef struct reiserfs_oid40 reiserfs_oid40_t;

#endif

