/*
    direntry40.h -- reiserfs default directory structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef DIRENTRY40_H
#define DIRENTRY40_H

struct reiserfs_objid {
    uint64_t locality;
    uint64_t objectid;
};

typedef struct reiserfs_objid reiserfs_objid_t;

#define objid_get_locality(objid)	    get_le64(objid, locality)
#define objid_set_locality(objid, val)	    set_le64(objid, locality, val)

#define objid_get_objectid(objid)	    get_le64(objid, objectid)
#define objid_set_objectid(objid, val)	    set_le64(objid, objectid, val)

struct reiserfs_entryid {
    uint64_t objectid;
    uint64_t offset;
};

typedef struct reiserfs_entryid reiserfs_entryid_t;

#define entryid_get_objectid(entryid)	    get_le64(entryid, objectid)
#define entryid_set_objectid(entryid, val)  set_le64(entryid, objectid, val)

#define entryid_get_offset(entryid)	    get_le64(entryid, offset)
#define entryid_set_offset(entryid, val)    set_le64(entryid, offset, val)

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

#define de40_get_count(de, num)		get_le16(de, count)
#define de40_set_count(de, num)		set_le16(de, count, num)

#define e40_get_offset(e, num)		get_le16(e, offset)
#define e40_set_offset(e, num)		set_le16(e, offset, num)

#endif

