/*
    item.c -- common reiser4 item functions.
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

errno_t reiser4_item_open(reiser4_item_t *item, 
    reiser4_node_t *node, reiser4_pos_t *pos)
{
    rpid_t pid;
    
    aal_assert("umka-1063", node != NULL, return -1);
    aal_assert("umka-1066", pos != NULL, return -1);
    aal_assert("umka-1064", item != NULL, return -1);
    
    pid = plugin_call(return 0, 
	node->entity->plugin->node_ops, item_pid, node->entity, pos);
    
    if (pid == INVALID_PLUGIN_ID) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Invalid item plugin id detected. Node %llu, item %u.", 
	    aal_block_number(node->block), pos->item);
	return -1;
    }
    
    item->plugin = libreiser4_factory_ifind(ITEM_PLUGIN_TYPE, pid);
    
    if (!item->plugin) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't get item plugin. Node %llu, item %u.", 
	    aal_block_number(node->block), pos->item);
	return -1;
    }
	    
    item->body = plugin_call(return -1, node->entity->plugin->node_ops, 
	item_body, node->entity, pos);
    
    if (!item->body) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get item body. Node %llu, item %u.", 
	    aal_block_number(node->block), pos->item);
	return -1;
    }

    item->len = plugin_call(return -1, node->entity->plugin->node_ops, 
	item_len, node->entity, pos);
    
    item->node = node;
    item->pos = pos;

    return 0;
}

errno_t reiser4_item_reopen(reiser4_item_t *item) {
    aal_assert("umka-1131", item != NULL, return -1);
    return reiser4_item_open(item, item->node, item->pos);
}

errno_t reiser4_item_init(reiser4_item_t *item, 
    reiser4_node_t *node, reiser4_pos_t *pos) 
{
    aal_assert("umka-1060", node != NULL, return -1);
    aal_assert("umka-1067", pos != NULL, return -1);
    
    item->node = node;
    item->pos = pos;
    
    return 0;
}

/* Returns count of units in item */
uint32_t reiser4_item_count(reiser4_item_t *item) {
    aal_assert("umka-1030", item != NULL, return 0);
    aal_assert("umka-1068", item->plugin != NULL, return 0);
    aal_assert("umka-1069", item->body != NULL, return 0);
    
    if (item->plugin->item_ops.common.count)
	return item->plugin->item_ops.common.count(item->body);

    return 1;
}

#ifndef ENABLE_COMPACT

/*
    We can estimate size for insertion and for pasting of hint->data (to be memcpy) 
    or of item_info->info (data to be created on the base of).
    
    1. Insertion of data: 
    a) pos->unit == ~0ul 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    2. Insertion of info: 
    a) pos->unit == ~0ul 
    b) hint->info != NULL
    c) hint->plugin != NULL
    
    3. Pasting of data: 
    a) pos->unit != ~0ul 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    4. Pasting of info: 
    a) pos->unit_pos != ~0ul 
    b) hint->info != NULL
    c) get hint->plugin on the base of pos.
*/
errno_t reiser4_item_estimate(
    reiser4_item_t *item,	/* item we will work with */
    reiser4_item_hint_t *hint	/* item hint to be estimated */
) {
    aal_assert("vpf-106", item != NULL, return -1);
    aal_assert("umka-541", hint != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", item->pos->unit != ~0ul || 
	hint->plugin != NULL, return -1);
   
    /* Here hint has been already set for the 3rd case */
    if (hint->data != NULL)
	return 0;
    
    /* Estimate for the 2nd and for the 4th cases */
    return plugin_call(return -1, hint->plugin->item_ops.common, 
	estimate, item->pos->unit, hint);
}

#endif

int reiser4_item_permissn(reiser4_item_t *item) {
    aal_assert("umka-1100", item != NULL, return 0);
    aal_assert("umka-1101", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == PERMISSN_ITEM_GROUP;
}

int reiser4_item_tail(reiser4_item_t *item) {
    aal_assert("umka-1098", item != NULL, return 0);
    aal_assert("umka-1099", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == TAIL_ITEM_GROUP;
}

int reiser4_item_extent(reiser4_item_t *item) {
    aal_assert("vpf-238", item != NULL, return 0);
    aal_assert("vpf-239", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == EXTENT_ITEM_GROUP;
}

int reiser4_item_direntry(reiser4_item_t *item) {
    aal_assert("umka-1096", item != NULL, return 0);
    aal_assert("umka-1097", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == DIRENTRY_ITEM_GROUP;
}

int reiser4_item_statdata(reiser4_item_t *item) {
    aal_assert("umka-1094", item != NULL, return 0);
    aal_assert("umka-1095", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == STATDATA_ITEM_GROUP;
}

uint16_t reiser4_item_get_smode(reiser4_item_t *item) {
    aal_assert("umka-1102", item != NULL, return 0);
    aal_assert("umka-1103", item->plugin != NULL, return 0);
    aal_assert("umka-1104", item->body != NULL, return 0);

    /* Checking if specified item is a statdata item */
    if (!reiser4_item_statdata(item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get access mode from non-statdata item.");
	return 0;
    }
    
    return plugin_call(return 0, item->plugin->item_ops.specific.statdata, 
	get_mode, item->body);
}

#ifndef ENABLE_COMPACT

errno_t reiser4_item_set_smode(reiser4_item_t *item, 
    uint16_t mode) 
{
    aal_assert("umka-1105", item != NULL, return 0);
    aal_assert("umka-1106", item->plugin != NULL, return 0);
    aal_assert("umka-1107", item->body != NULL, return 0);

    /* Checking if specified item is a statdata item */
    if (!reiser4_item_statdata(item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get access mode from non-statdata item.");
	return -1;
    }
    
    return plugin_call(return -1, item->plugin->item_ops.specific.statdata, 
	set_mode, item->body, mode);
}

#endif

int reiser4_item_internal(reiser4_item_t *item) {
    aal_assert("vpf-042", item != NULL, return 0);
    aal_assert("umka-1072", item->plugin != NULL, return 0);

    return item->plugin->h.type == ITEM_PLUGIN_TYPE &&
	item->plugin->item_ops.group == INTERNAL_ITEM_GROUP;
}

/* Returns node pointer from internal node */
blk_t reiser4_item_get_iptr(reiser4_item_t *item) {
    aal_assert("vpf-041", item != NULL, return 0);
    aal_assert("umka-1073", item->body != NULL, return 0);
    aal_assert("umka-1074", item->plugin != NULL, return 0);
    
    /* Checking if specified item is an internal item */
    if (!reiser4_item_internal(item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get the node pointer from non-internal item.");
	return 0;
    }
    
    return plugin_call(return 0, item->plugin->item_ops.specific.internal, 
	get_ptr, item->body);
}

#ifndef ENABLE_COMPACT

/* Updates node pointer in internal item specified by "pos" */
errno_t reiser4_item_set_iptr(reiser4_item_t *item,
    blk_t blk) 
{
    aal_assert("umka-607", item != NULL, return -1);
    aal_assert("umka-1070", item->body != NULL, return -1);
    aal_assert("umka-1071", item->plugin != NULL, return -1);

    /* Checking if specified item is an internal item */
    if (!reiser4_item_internal(item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to set up the node pointer "
	    "for non-internal item.");
	return -1;
    }
    
    /* Calling node plugin for handling */
    return plugin_call(return -1, item->plugin->item_ops.specific.internal, 
	set_ptr, item->body, blk);
}

#endif

uint32_t reiser4_item_len(reiser4_item_t *item) {
    aal_assert("umka-760", item != NULL, return 0);
    return item->len;
}

reiser4_body_t *reiser4_item_body(reiser4_item_t *item) {
    aal_assert("umka-554", item != NULL, return NULL);
    return item->body;
}

reiser4_plugin_t *reiser4_item_plugin(reiser4_item_t *item) {
    aal_assert("umka-755", item != NULL, return NULL);
    return item->plugin;
}

