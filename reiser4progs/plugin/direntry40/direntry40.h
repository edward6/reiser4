/*
    direntry40.h -- reiserfs default directory structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef DIRENTRY40_H
#define DIRENTRY40_H

#include <reiser4/key.h>

struct reiserfs_objid {
    uint8_t locality[sizeof(uint64_t)];
    uint8_t objectid[sizeof(uint64_t)];
};

typedef struct reiserfs_objid reiserfs_objid_t;

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

#define direntry40_get_count(de, num)	    get_le16(de, count)
#define direntry40_set_count(de, num)	    set_le16(de, count, num)

#define entry40_get_offset(entry, num)	    get_le16(entry, offset)
#define entry40_set_offset(entry, num)	    set_le16(entry, offset, num)

#define direntry40_get_entry_num(de, num)   set_le16(de, entry_num, num)
#define direntry40_set_entry_num(de, num)   set_le16(de, entry_num, num)

#endif

