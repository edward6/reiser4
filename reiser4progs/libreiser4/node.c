/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk,
    reiserfs_id_t node_pid, reiserfs_id_t key_pid, uint8_t level)
{
    reiserfs_node_t *node;
    
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    if (!(node->block = aal_block_alloc(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }
    
    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_pid))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_pid);

    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_pid))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_pid);
   
    /* Requesting the plugin for initialization of the entity */
    if (!(node->entity = libreiser4_plugin_call(goto error_free_block, 
	node->node_plugin->node_ops, create, node->block, level))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create node entity.");
	goto error_free_block;
    }
    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:    
    aal_free(node);

    return NULL;
}

errno_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-798", node != NULL, return -1);
    return aal_block_write(node->block);
}

errno_t reiserfs_node_set_key(reiserfs_node_t *node, uint32_t pos, 
    reiserfs_key_t *key)
{
    aal_assert("umka-804", node != NULL, return -1);
    aal_assert("umka-805", key != NULL, return -1);

    libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	set_key, node->entity, pos, key);
    
    return 0;
}

#endif

reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_id_t key_pid) 
{
    reiserfs_node_t *node;
    reiserfs_id_t node_pid;

    aal_assert("umka-160", device != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    if (!(node->block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu. %s.", blk, aal_device_error(device));
	goto error_free_node;
    }
    
    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_pid))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_pid);
    
    node_pid = *((uint16_t *)node->block->data);
    
    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_pid))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_pid);

    if (!(node->entity = libreiser4_plugin_call(goto error_free_block, 
	node->node_plugin->node_ops, open, node->block)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialize node entity.");
	goto error_free_block;
    }    
	    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:
    aal_free(node);
    return NULL;
}

errno_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-824", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops,
	close, node->entity);
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

errno_t reiserfs_node_rdkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_node_get_key(node, reiserfs_node_count(node) - 1, key);
    return 0;
}

errno_t reiserfs_node_ldkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_node_get_key(node, 0, key);
    return 0;
}

static errno_t reiserfs_node_relocate(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos, int remove) 
{
    errno_t res;
    reiserfs_id_t pid;
    reiserfs_item_hint_t item;

    aal_assert("umka-799", src_node != NULL, return -1);
    aal_assert("umka-800", dst_node != NULL, return -1);

    item.data = reiserfs_node_item_body(src_node, src_pos->item);
    item.len = reiserfs_node_item_len(src_node, src_pos->item);
    
    /* Getting the key of item that is going to be copied */
    reiserfs_node_get_key(src_node, src_pos->item, (reiserfs_key_t *)&item.key);
    
    pid = reiserfs_node_item_get_pid(src_node, src_pos->item);
	
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, pid)))
	libreiser4_factory_failed(return -1, find, item, pid);

    /* Insering the item into new location */
    if ((res = reiserfs_node_insert(dst_node, dst_pos, &item)))
	return res;
    
    /* Remove src item if remove flag is turned on */
    if (remove)
	res = reiserfs_node_remove(src_node, src_pos);
    
    return res;
}

errno_t reiserfs_node_copy(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos) 
{
    return reiserfs_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 0);
}

errno_t reiserfs_node_move(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos) 
{
    return reiserfs_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 1);
}

errno_t reiserfs_node_split(reiserfs_node_t *node, 
    reiserfs_node_t *right) 
{
    uint32_t median;
    reiserfs_pos_t dst_pos, src_pos;
    
    aal_assert("umka-780", node != NULL, return -1);
    aal_assert("umka-781", right != NULL, return -1);

    median = reiserfs_node_count(node) / 2;
    while (reiserfs_node_count(node) > median) {
	src_pos.item = reiserfs_node_count(node) - 1;
	src_pos.unit = 0xffff;
	
	dst_pos.item = 0;
	dst_pos.unit = 0xffff;
	
	if (reiserfs_node_move(right, &dst_pos, node, &src_pos))
	    return -1;
    }
    
    return 0;
}

int reiserfs_node_confirm(reiserfs_node_t *node) {
    aal_assert("umka-123", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	confirm, node->entity);
}

/* Checks node for validness */
errno_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	check, node->entity, flags);
}

int reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key, 
    reiserfs_pos_t *pos)
{
    uint32_t item_pos;
    int lookup; void *body;
    reiserfs_key_t max_key;
    reiserfs_plugin_t *item_plugin;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);

    pos->item = 0;
    pos->unit = 0xffff;

    if (reiserfs_node_count(node) == 0)
	return 0;
   
    if ((lookup = libreiser4_plugin_call(return -1, 
	node->node_plugin->node_ops, lookup, node->entity, key, pos)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_get_nr(node->block));
	return -1;
    }

    if (lookup == 1) return 1;

    item_pos = pos->item - (pos->item > 0 ? 1 : 0);
	    
    if (!(item_plugin = reiserfs_node_item_get_plugin(node, item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), item_pos);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. Key could lies 
	within the item or after the item.
    */
    reiserfs_node_get_key(node, item_pos, &max_key);
    
    if (item_plugin->item_ops.common.maxkey) {
	    
	if (item_plugin->item_ops.common.maxkey(&max_key) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_get_nr(node->block));
	    return -1;
	}
	
	if (libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	    compare, key->body, &max_key.body) > 0)
	{
	    pos->item++;
	    return lookup;
	}
    }

    if (!item_plugin->item_ops.common.lookup)
	return lookup;
	    
    if (!(body = reiserfs_node_item_body(node, item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), item_pos);
	return -1;
    }
    
    if ((lookup = item_plugin->item_ops.common.lookup(body, key, 
	&pos->unit)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    item_pos, aal_block_get_nr(node->block));
	return -1;
    }
    
    return lookup;
}

/* Removes specified by pos item from node */
errno_t reiserfs_node_remove(reiserfs_node_t *node, reiserfs_pos_t *pos) {
    aal_assert("umka-767", node != NULL, return -1);
    aal_assert("umka-768", pos != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	remove, node->entity, pos);
}

uint32_t reiserfs_node_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops,
	maxnum, node->entity);
}

uint32_t reiserfs_node_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	count, node->entity);
}

errno_t reiserfs_node_insert(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item) 
{
    errno_t ret;
    
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", item != NULL, return -1);
    aal_assert("vpf-108", pos != NULL, return -1);

    if (!item->data) {
	/* 
	    Estimate the size that will be spent for item. This should be done
	    if item->data not installed.
	*/
	if (reiserfs_node_item_estimate(node, pos, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't estimate space that item being inserted will consume.");
	    return -1;
	}
    } else {
	aal_assert("umka-761", item->len > 0 && 
	    item->len < node->block->size, return -1);
    }
    
    if (item->len + reiserfs_node_item_overhead(node) >
        reiserfs_node_get_free_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the item of (%u) size in the node (%llu).",
            item->len, aal_block_get_nr(node->block));
        return -1;
    }

    if (pos->unit == 0xffff) {
        if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
		insert, node->entity, pos, item)) != 0)
	    return ret;
    } else {
	if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
		paste, node->entity, pos, item)) != 0)
	    return ret;
    }
    
    return 0;
}

#ifndef ENABLE_COMPACT

errno_t reiserfs_node_set_pid(reiserfs_node_t *node, uint32_t pid) {
    aal_assert("umka-828", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops,
	set_pid, node->entity, pid);
}

errno_t reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return -1);

    if (node->node_plugin->node_ops.set_level == NULL)
        return 0;

    return node->node_plugin->node_ops.set_level(node->entity, level);
}

errno_t reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	set_free_space, node->entity, value);
}

#endif

uint32_t reiserfs_node_get_pid(reiserfs_node_t *node) {
    aal_assert("umka-828", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops,
	get_pid, node->entity);
}

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->node_plugin->node_ops.get_level == NULL)
        return 0;

    return node->node_plugin->node_ops.get_level(node->entity);
}

uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	get_free_space, node->entity);
}

uint32_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	item_overhead,);
}

uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	item_maxsize, node->entity);
}

uint32_t reiserfs_node_item_len(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-760", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	item_len, node->entity, pos);
}

void *reiserfs_node_item_body(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->node_plugin->node_ops, 
	item_body, node->entity, pos);
}

errno_t reiserfs_node_get_key(reiserfs_node_t *node, uint32_t pos, 
    reiserfs_key_t *key) 
{
    aal_assert("umka-565", node != NULL, return -1);
    aal_assert("umka-803", key != NULL, return -1);
    
    key->plugin = node->key_plugin;
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	get_key, node->entity, pos, key);
}

reiserfs_id_t reiserfs_node_item_get_pid(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node_ops, 
	item_get_pid, node->entity, pos);
}

reiserfs_plugin_t *reiserfs_node_item_get_plugin(reiserfs_node_t *node, 
    uint32_t pos) 
{
    aal_assert("umka-755", node != NULL, return NULL);
    
    return libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 
	reiserfs_node_item_get_pid(node, pos));
}

blk_t reiserfs_node_get_pointer(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);
    aal_assert("umka-778", pos < reiserfs_node_count(node), return 0);

    if (!reiserfs_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get the node pointer from non-internal item.");
	return 0;
    }
    
    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find internal item plugin.");
	return 0;
    }

    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	get_pointer, body);
}

int reiserfs_node_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return 0);

    if (!reiserfs_node_item_internal(node, pos))
	return 0;

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	has_pointer, body, blk);
}

int reiserfs_node_item_internal(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("vpf-042", node != NULL, return 0);
    return reiserfs_node_item_get_pid(node, pos) == REISERFS_INTERNAL_ITEM;
}

#ifndef ENABLE_COMPACT

errno_t reiserfs_node_item_set_pid(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_id_t pid)
{
    aal_assert("umka-551", node != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->node_plugin->node_ops, 
	item_set_pid, node->entity, pos, pid);
}

errno_t reiserfs_node_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return -1);

    if (!reiserfs_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to set the node pointer inside non-internal item.");
	return -1;
    }
    
    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }
    
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return -1;
    }
    
    return libreiser4_plugin_call(return -1, plugin->item_ops.specific.internal, 
	set_pointer, body, blk);
}

/*
    We can estimate size for insertion and for pasting of hint->data (to be memcpy) 
    or of item_info->info (data to be created on the base of).
    
    1. Insertion of data: 
    a) pos->unit == 0xffff 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    2. Insertion of info: 
    a) pos->unit == 0xffff 
    b) hint->info != NULL
    c) hint->plugin != NULL
    
    3. Pasting of data: 
    a) pos->unit != 0xffff 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    4. Pasting of info: 
    a) pos->unit_pos != 0xffff 
    b) hint->info != NULL
    c) get hint->plugin on the base of pos.
*/

errno_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item)
{
    aal_assert("vpf-106", item != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", pos != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", pos->unit != 0xffff || 
	item->plugin != NULL, return -1);
   
    if (!item->plugin && !(item->plugin = 
	reiserfs_node_item_get_plugin(node, pos->item))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }

    /* Here hint has been already set for the 3rd case */
    if (item->data != NULL)
	return 0;
    
    /* Estimate for the 2nd and for the 4th cases */
    return libreiser4_plugin_call(return -1, item->plugin->item_ops.common, 
	estimate, pos->unit, item);
}

#endif

