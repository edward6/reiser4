/*
    key.h -- reiser4 key structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef KEY_H
#define KEY_H

#include <aal/aal.h>
#include <reiser4/filesystem.h>

typedef enum {
    /* file name */
    KEY_FILE_NAME_MINOR = 0,
    
    /* stat-data */
    KEY_SD_MINOR        = 1,
    
    /* file attribute name */
    KEY_ATTR_NAME_MINOR = 2,
    
    /* file attribute value */
    KEY_ATTR_BODY_MINOR = 3,
    
    /* file body (tail or extent) */
    KEY_BODY_MINOR      = 4
} key_minor_locality;

typedef enum {
    /* major "locale", aka dirid. Sits in 1st element */
    KEY_LOCALITY_INDEX   = 0,
    
    /* minor "locale", aka item type. Sits in 1st element */
    KEY_TYPE_INDEX       = 0,
    
    /* "object band". Sits in 2nd element */
    KEY_BAND_INDEX       = 1,
    
    /* objectid. Sits in 2nd element */
    KEY_OBJECTID_INDEX   = 1,
    
    /* Offset. Sits in 3rd element */
    KEY_OFFSET_INDEX     = 2,
    
    /* Name hash. Sits in 3rd element */
    KEY_HASH_INDEX       = 2,
    KEY_LAST_INDEX       = 3
} reiserfs_key_field_index;

union reiserfs_key {
    uint64_t el[KEY_LAST_INDEX];
    int pad;
};

typedef union reiserfs_key reiserfs_key_t;

struct reiserfs_entryid {
    uint8_t objectid[sizeof(uint64_t)];
    uint8_t hash[sizeof(uint64_t)];
};

typedef struct reiserfs_entryid reiserfs_entryid_t;

typedef enum {
    /* major locality occupies higher 60 bits of the first element */
    KEY_LOCALITY_MASK    = 0xfffffffffffffff0ull,
    
    /* minor locality occupies lower 4 bits of the first element */
    KEY_TYPE_MASK        = 0xfull,
    
    /* controversial band occupies higher 4 bits of the 2nd element */
    KEY_BAND_MASK        = 0xf000000000000000ull,
    
    /* objectid occupies lower 60 bits of the 2nd element */
    KEY_OBJECTID_MASK    = 0x0fffffffffffffffull,
    
    /* offset is just 3rd L.M.Nt itself */
    KEY_OFFSET_MASK      = 0xffffffffffffffffull,
    
    /* hash occupies 56 higher bits of 3rd element */
    KEY_HASH_MASK        = 0xffffffffffffff00ull,
    
    /* generation counter occupies lower 8 bits of 3rd element */
    KEY_GEN_MASK         = 0xffull,
} reiserfs_key_field_mask;

#define OID_CHARS (sizeof(uint64_t) - 1)

typedef enum {
    KEY_LOCALITY_SHIFT   = 4,
    KEY_TYPE_SHIFT       = 0,
    KEY_BAND_SHIFT       = 60,
    KEY_OBJECTID_SHIFT   = 0,
    KEY_OFFSET_SHIFT     = 0,
    KEY_HASH_SHIFT       = 8,
    KEY_GEN_SHIFT        = 0,
} reiserfs_key_field_shift;

static inline uint64_t get_key_el(const reiserfs_key_t *key,
    reiserfs_key_field_index off)
{
    aal_assert("vpf-029", key != NULL,  return 0);
    aal_assert("vpf-030", off < KEY_LAST_INDEX, return 0);
    return LE64_TO_CPU(key->el[off]);
}

static inline void set_key_el(reiserfs_key_t *key,
    reiserfs_key_field_index off, uint64_t value)
{
    aal_assert("vpf-031", key != NULL, return);
    aal_assert("vpf-032", off < KEY_LAST_INDEX, return);
    key->el[off] = CPU_TO_LE64(value);
}

/* Macro to define getter and setter functions for field F with type T */
static inline int get_key_locality2 (const reiserfs_key_t *key) {
    aal_assert("vpf-035", key != NULL, return 0);
    return (int)(get_key_el(key, KEY_LOCALITY_INDEX) &
	KEY_LOCALITY_MASK) >> KEY_LOCALITY_SHIFT;
}

#define DEFINE_KEY_FIELD(L, U, T)				\
static inline T get_key_ ## L (const reiserfs_key_t *key) {	\
    aal_assert("vpf-036", key != NULL, return 0);		\
    return (T) ((get_key_el(key, KEY_##U##_INDEX) &		\
	KEY_##U##_MASK) >> KEY_##U##_SHIFT);			\
}								\
								\
static inline void set_key_##L(reiserfs_key_t *key, T loc) {	\
    uint64_t el;						\
								\
    aal_assert("vpf-033", key != NULL, return);			\
								\
    el = get_key_el(key, KEY_##U##_INDEX);			\
								\
    /* clear field bits in the key */				\
    el &= ~KEY_##U##_MASK;					\
								\
    aal_assert("vpf-034", ((loc << KEY_##U##_SHIFT) &		\
        ~KEY_##U##_MASK) == 0, return);				\
								\
    el |= (loc << KEY_##U##_SHIFT);				\
    set_key_el(key, KEY_##U##_INDEX, el);			\
}

/* define get_key_locality(), set_key_locality() */
DEFINE_KEY_FIELD(locality, LOCALITY, uint64_t);

/* define get_key_type(), set_key_type() */
DEFINE_KEY_FIELD(type, TYPE, key_minor_locality);

/* define get_key_band(), set_key_band() */
DEFINE_KEY_FIELD(band, BAND, uint64_t);

/* define get_key_objectid(), set_key_objectid() */
DEFINE_KEY_FIELD(objectid, OBJECTID, uint64_t);

/* define get_key_offset(), set_key_offset() */
DEFINE_KEY_FIELD(offset, OFFSET, uint64_t);

/* define get_key_hash(), set_key_hash() */
DEFINE_KEY_FIELD(hash, HASH, uint64_t);

extern void reiserfs_key_init(reiserfs_key_t *key);
extern void build_key_by_entryid(reiserfs_key_t *key, reiserfs_entryid_t *entryid);
extern void build_entryid_by_entry_info(reiserfs_entryid_t *entryid, 
    reiserfs_entry_info_t *info);

#endif

