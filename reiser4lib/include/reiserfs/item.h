/*
    item.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef ITEM_H
#define ITEM_H

#include <reiserfs/plugin.h>

/* On memory structure to work with items */
/* Thougth: the key should not exist here, we should get it from item. */
struct reiserfs_item_info {
//    reiserfs_plugin_id_t plugin_id;
    int plugin_id;
    reiserfs_key_t *key;
    
    uint32_t length;
    void *data;
    void *arg; 
};

typedef struct reiserfs_item_info reiserfs_item_info_t;

#endif

