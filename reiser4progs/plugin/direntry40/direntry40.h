/*
    direntry40.h -- reiserfs default directory structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef DIRENTRY40_H
#define DIRENTRY40_H

struct reiserfs_objid {
    uint8_t locality[sizeof(uint64_t)];
    uint8_t objectid[sizeof(uint64_t)];
};

typedef struct reiserfs_objid reiserfs_objid_t;

#define oid_get_locality(oid)		    LE64_TO_CPU(*((uint64_t *)oid->locality))
#define oid_set_locality(oid, val)	    (*(uint64_t *)oid->locality) = CPU_TO_LE64(val)

#define oid_get_objectid(oid)		    LE64_TO_CPU(*((uint64_t *)oid->objectid))
#define oid_set_objectid(oid, val)	    (*(uint64_t *)oid->objectid) = CPU_TO_LE64(val)

struct reiserfs_entryid {
    uint8_t objectid[sizeof(uint64_t)];
    uint8_t offset[sizeof(uint64_t)];
};

typedef struct reiserfs_entryid reiserfs_entryid_t;

#define eid_get_objectid(eid)		    LE64_TO_CPU(*((uint64_t *)eid->objectid))
#define eid_set_objectid(eid, val)	    (*(uint64_t *)eid->objectid) = CPU_TO_LE64(val)

#define eid_get_offset(eid)		    LE64_TO_CPU(*((uint64_t *)eid->offset))
#define eid_set_offset(eid, val)	    (*(uint64_t *)eid->offset) = CPU_TO_LE64(val)

struct reiserfs_entry40 {
    reiserfs_entryid_t entryid;
    uint16_t offset;
};

typedef struct reiserfs_entry40 reiserfs_entry40_t;

struct reiserfs_direntry40 {
    uint16_t count;
    reiserfs_entry40_t entry[0];
};

typedef struct reiserfs_direntry40 reiserfs_direntry40_t;

#define de40_get_count(de)		    aal_get_le16(de, count)
#define de40_set_count(de, num)		    aal_set_le16(de, count, num)

#define en40_get_offset(en)		    aal_get_le16(en, offset)
#define en40_set_offset(en, num)	    aal_set_le16(en, offset, num)

#endif

