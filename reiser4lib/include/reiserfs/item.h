/*
    key.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/


/* on memory structure to wotk with items */
struct reiserfs_item_data {
    reiserfs_key_t * key;
    uint32_t lenght;
    void *data;
    void *arg;    
};

typedef struct reiserfs_item_data reiserfs_item_data_t;

