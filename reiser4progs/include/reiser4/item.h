/*
    item.h -- common item functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ITEM_H
#define ITEM_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/filesystem.h>

extern errno_t reiser4_item_init(reiser4_item_t *item, 
    reiser4_node_t *node, reiser4_pos_t *pos);

extern errno_t reiser4_item_open(reiser4_item_t *item, 
    reiser4_node_t *node, reiser4_pos_t *pos);

extern errno_t reiser4_item_reopen(reiser4_item_t *item);
extern uint32_t reiser4_item_count(reiser4_item_t *item);

#ifndef ENABLE_COMPACT
extern errno_t reiser4_item_estimate(reiser4_item_t *item,
    reiser4_item_hint_t *hint);
#endif

extern uint32_t reiser4_item_len(reiser4_item_t *item);
extern reiser4_body_t *reiser4_item_body(reiser4_item_t *item);
extern reiser4_plugin_t *reiser4_item_plugin(reiser4_item_t *item);

/* Internal item methods */
extern int reiser4_item_internal(reiser4_item_t *item);
extern blk_t reiser4_item_get_iptr(reiser4_item_t *item);

#ifndef ENABLE_COMPACT

extern errno_t reiser4_item_set_iptr(reiser4_item_t *item,
    blk_t blk); 

#endif

/* Statdata item methods */
extern int reiser4_item_statdata(reiser4_item_t *item);
extern uint16_t reiser4_item_get_smode(reiser4_item_t *item);

#ifndef ENABLE_COMPACT

extern errno_t reiser4_item_set_smode(reiser4_item_t *item,
    uint16_t mode);

#endif

/* Permission item methods */
extern int reiser4_item_permissn(reiser4_item_t *item);

/* Filebody item methods */
extern int reiser4_item_filebody(reiser4_item_t *item);

/* Direntry item methods */
extern int reiser4_item_direntry(reiser4_item_t *item);

#endif

