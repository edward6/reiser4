/*
    item.h -- reiser4 item structures.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifndef ITEM_H
#define ITEM_H

/* On memory structure to work with items */
/* Thougth: the key should not exist here, we should get it from item. */
struct reiserfs_item {
    reiserfs_key_t *key;
    uint32_t length;
    reiserfs_opaque_t *entity;
    reiserfs_plugin_t *plugin;
};

typedef struct reiserfs_item reiserfs_item_t;

extern blk_t down_link (reiserfs_item_t *);

#endif

