/*
    key.h -- reiser4 key structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

typedef enum {
    /** major "locale", aka dirid. Sits in 1st element */
    KEY_LOCALITY_INDEX   = 0,
    /** minor "locale", aka item type. Sits in 1st element */
    KEY_TYPE_INDEX       = 0,
    /** "object band". Sits in 2nd element */
    KEY_BAND_INDEX       = 1,
    /** objectid. Sits in 2nd element */
    KEY_OBJECTID_INDEX   = 1,
    /** Offset. Sits in 3rd element */
    KEY_OFFSET_INDEX     = 2,
    /** Name hash. Sits in 3rd element */
    KEY_HASH_INDEX       = 2,
    KEY_LAST_INDEX       = 3
} reiserfs_key_field_index;

union reiserfs_key {
    uint64_t el[ KEY_LAST_INDEX ];
    int pad;
};

typedef union reiserfs_key reiserfs_key_t;

