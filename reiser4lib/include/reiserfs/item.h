/*
    item.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef ITEM_H
#define ITEM_H

/* On memory structure to work with items */
struct reiserfs_item_data {
    reiserfs_key_t * key;
    uint32_t length;
    void *data;
    void *arg;    
};

typedef struct reiserfs_item_data reiserfs_item_data_t;

#endif

