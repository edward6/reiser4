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

#define objid_get_locality(objid)	    LE64_TO_CPU(*((uint64_t *)objid->locality))
#define objid_set_locality(objid, val)	    (*(uint64_t *)objid->locality) = CPU_TO_LE64(val)

#define objid_get_objectid(objid)	    LE64_TO_CPU(*((uint64_t *)objid->objectid))
#define objid_set_objectid(objid, val)	    (*(uint64_t *)objid->objectid) = CPU_TO_LE64(val)

struct reiserfs_entryid {
    uint8_t objectid[sizeof(uint64_t)];
    uint8_t offset[sizeof(uint64_t)];
};

typedef struct reiserfs_entryid reiserfs_entryid_t;

#define entryid_get_objectid(entryid)	    LE64_TO_CPU(*((uint64_t *)entryid->objectid))
#define entryid_set_objectid(entryid, val)  (*(uint64_t *)entryid->objectid) = CPU_TO_LE64(val)

#define entryid_get_offset(entryid)	    LE64_TO_CPU(*((uint64_t *)entryid->offset))
#define entryid_set_offset(entryid, val)    (*(uint64_t *)entryid->offset) = CPU_TO_LE64(val)

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

#define de40_get_count(de, num)		    aal_get_le16(de, count)
#define de40_set_count(de, num)		    aal_set_le16(de, count, num)

#define e40_get_offset(e, num)		    aal_get_le16(e, offset)
#define e40_set_offset(e, num)		    aal_set_le16(e, offset, num)

#endif

