/*
    direntry40.h -- reiser4 default directory structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef DIRENTRY40_H
#define DIRENTRY40_H

#include <aal/aal.h>
#include <aux/aux.h>
#include <reiser4/plugin.h>

struct objid40 {
    d8_t locality[sizeof(d64_t)];
    d8_t objectid[sizeof(d64_t)];
};

typedef struct objid40 objid40_t;

#define oid_get_locality(oid)		    LE64_TO_CPU(*((d64_t *)oid->locality))
#define oid_set_locality(oid, val)	    (*(d64_t *)oid->locality) = CPU_TO_LE64(val)

#define oid_get_objectid(oid)		    LE64_TO_CPU(*((d64_t *)oid->objectid))
#define oid_set_objectid(oid, val)	    (*(d64_t *)oid->objectid) = CPU_TO_LE64(val)

struct entryid40 {
    d8_t objectid[sizeof(uint64_t)];
    d8_t offset[sizeof(uint64_t)];
};

typedef struct entryid40 entryid40_t;

#define eid_get_objectid(eid)		    LE64_TO_CPU(*((d64_t *)eid->objectid))
#define eid_set_objectid(eid, val)	    (*(d64_t *)eid->objectid) = CPU_TO_LE64(val)

#define eid_get_offset(eid)		    LE64_TO_CPU(*((d64_t *)eid->offset))
#define eid_set_offset(eid, val)	    (*(d64_t *)eid->offset) = CPU_TO_LE64(val)

struct entry40 {
    entryid40_t entryid;
    d16_t offset;
};

typedef struct entry40 entry40_t;

struct direntry40 {
    d16_t count;
    entry40_t entry[0];
};

typedef struct direntry40 direntry40_t;

#define de40_get_count(de)		    aal_get_le16(de, count)
#define de40_set_count(de, num)		    aal_set_le16(de, count, num)

#define en40_get_offset(en)		    aal_get_le16(en, offset)
#define en40_set_offset(en, num)	    aal_set_le16(en, offset, num)

#endif
