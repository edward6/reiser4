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

errno_t reiserfs_node_rdkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_key_init(key, reiserfs_node_item_key(node, 
	reiserfs_node_count(node) - 1), node->key_plugin);

    return 0;
}

errno_t reiserfs_node_ldkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_key_init(key, reiserfs_node_item_key(node, 0), 
	node->key_plugin);
    
    return 0;
}

reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk,
    reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id, uint8_t level)
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
    
    aal_set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, node_plugin_id);

    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_plugin_id);

    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_plugin_id);
    
    if (node->node_plugin->node.create != NULL ) {
        if (node->node_plugin->node.create(node->block, level)) {
            aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Node plugin hasn't been able to create a node on block %llu.", blk);
            goto error_free_block;
        }
    }
     
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:    
    aal_free(node);

    return NULL;
}

#endif

reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id) 
{
    reiserfs_node_t *node;

    aal_assert("umka-160", device != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    if (!(node->block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu. %s.", blk, aal_device_error(device));
	goto error_free_node;
    }
    
    if (node_plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	node_plugin_id = reiserfs_node_get_plugin_id(node);
    
    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_plugin_id);

    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_plugin_id);
    
    if (node->node_plugin->node.open != NULL) {
        if (node->node_plugin->node.open (node->block)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Node plugin hasn't been able to open a node %llu.", blk);
            goto error_free_block;
        }
    }
    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:
    aal_free(node);

    return NULL;
}

errno_t reiserfs_node_reopen(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk, reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id) 
{
    aal_assert("umka-724", node != NULL, return -1);
    aal_assert("umka-725", device != NULL, return -1);
    
    return -1;
}

errno_t reiserfs_node_close(reiserfs_node_t *node) {
    if (node->node_plugin->node.close != NULL) {
        if (node->node_plugin->node.close (node->block)) {
            aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Can't close node (%llu).", aal_block_get_nr(node->block));
            return -1;
        }
    }
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

errno_t reiserfs_node_split(reiserfs_node_t *node, 
    reiserfs_node_t *right) 
{
/*    uint32_t median;
    reiserfs_coord_t dst, src;
    
    aal_assert("umka-780", node != NULL, return -1);
    aal_assert("umka-781", right != NULL, return -1);

    median = reiserfs_node_count(node) / 2;
    while (reiserfs_node_count(node) > median) {
	reiserfs_coord_init(&src, node, reiserfs_node_count(node) - 1, 0xffff);	    
	reiserfs_coord_init(&dst, right, 0, 0xffff);
	
	if (reiserfs_node_move_item(&dst, &src, node->key_plugin))
	    return -1;
    }*/
    
    return 0;
}

/* Checks node for validness */
errno_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->node_plugin->node, 
	check, node->block, flags);
}

int reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key, 
    reiserfs_pos_t *pos)
{
    int lookup; void *body;
    reiserfs_plugin_t *item_plugin;
    reiserfs_key_t max_key;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);

    pos->item = 0;
    pos->unit = 0xffff;

    if ((lookup = libreiser4_plugin_call(return -1, 
	node->node_plugin->node, lookup, node->block, pos, key)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_get_nr(node->block));
	return -1;
    }

    if (lookup == 1)
	return 1;
   
    if (!(item_plugin = reiserfs_node_get_item_plugin(node, 
	pos->item > 0 ? pos->item - 1 : 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. Key could lies 
	within the item or after the item.
    */
    reiserfs_key_init(&max_key, reiserfs_node_item_key(node, 
	pos->item), key->plugin);
    
    if (item_plugin->item.common.maxkey) {
	    
	if (item_plugin->item.common.maxkey(&max_key) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_get_nr(node->block));
	    return -1;
	}
	
	if (libreiser4_plugin_call(return -1, key->plugin->key, 
	    compare, key->body, &max_key.body) > 0)
	{
	    pos->item++;
	    return lookup;
	}
    }

    if (!item_plugin->item.common.lookup)
	return lookup;
	    
    if (!(body = reiserfs_node_item_body(node, pos->item))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }
    
    if ((lookup = item_plugin->item.common.lookup(body, key, pos)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    pos->item, aal_block_get_nr(node->block));
	return -1;
    }
    
    return lookup;
}

/* Removes specified by pos item from node */
errno_t reiserfs_node_remove(reiserfs_node_t *node, reiserfs_pos_t *pos) {
    aal_assert("umka-767", node != NULL, return -1);
    aal_assert("umka-768", pos != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->node_plugin->node, 
	remove, node->block, pos);
}

errno_t reiserfs_node_embed_key(reiserfs_node_t *node, uint32_t pos, 
    reiserfs_key_t *key) 
{
    void *key_p;
    
    aal_assert("umka-772", node != NULL, return -1);
    aal_assert("umka-774", key != NULL, return -1);
    aal_assert("umka-775", key->plugin != NULL, return -1);
    
    aal_assert("umka-773", pos < reiserfs_node_count(node), return -1);

    if (!(key_p = reiserfs_node_item_key(node, pos)))
	return -1;
    
    aal_memcpy(key_p, key->body, libreiser4_plugin_call(return -1, 
	key->plugin->key, size,));
    
    return 0;
}

uint16_t reiserfs_node_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node,
	maxnum, node->block);
}

uint16_t reiserfs_node_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	count, node->block);
}

errno_t reiserfs_node_insert(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_key_t *key, 
    reiserfs_item_hint_t *item) 
{
    errno_t ret;
    
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("umka-720", key->plugin != NULL, return -1);
    
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", item != NULL, return -1);
    aal_assert("vpf-108", pos != NULL, return -1);

    if (!item->data) {
	/* 
	    Estimate the size that will be spent for item. This should be done
	    if item->data not installed.
	*/
	if (reiserfs_node_item_estimate(node, item, pos)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't estimate space that item being inserted will consume.");
	    return -1;
	}
    } else {
	aal_assert("umka-761", item->length > 0 && 
	    item->length < node->block->size - sizeof(reiserfs_node_header_t), return -1);
    }
    
    if (item->length + reiserfs_node_item_overhead(node) >
        reiserfs_node_get_free_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the item of (%u) size in the node (%llu).",
            item->length, aal_block_get_nr(node->block));
        return -1;
    }

    if (pos->unit == 0xffff) {
        if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node, 
		insert, node->block, pos, key, item)) != 0)
	    return ret;
    } else {
	if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node, 
		paste, node->block, pos, key, item)) != 0)
	    return ret;
    }
    
    return 0;
}

#ifndef ENABLE_COMPACT

errno_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-798", node != NULL, return -1);
    return aal_block_write(node->block);
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);

    if (node->node_plugin->node.set_level == NULL)
        return;

    node->node_plugin->node.set_level(node->block, level);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    return libreiser4_plugin_call(return, node->node_plugin->node, 
	set_free_space, node->block, value);
}

void reiserfs_node_set_plugin_id(reiserfs_node_t *node, reiserfs_id_t plugin_id) {
    aal_assert("umka-603", node != NULL, return);
    aal_set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, plugin_id);
}

#endif

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->node_plugin->node.get_level == NULL)
        return 0;

    return node->node_plugin->node.get_level(node->block);
}

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	get_free_space, node->block);
}

reiserfs_id_t reiserfs_node_get_plugin_id(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return aal_get_le16((reiserfs_node_header_t *)node->block->data, plugin_id);
}

uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_overhead, node->block);
}

uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_maxsize, node->block);
}

uint16_t reiserfs_node_item_length(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-760", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_length, node->block, pos);
}

void *reiserfs_node_item_body(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->node_plugin->node, 
	item_body, node->block, pos);
}

void *reiserfs_node_item_key(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-565", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->node_plugin->node, 
	item_key, node->block, pos);
}

reiserfs_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	get_item_plugin_id, node->block, pos);
}

reiserfs_plugin_t *reiserfs_node_get_item_plugin(reiserfs_node_t *node, 
    uint32_t pos) 
{
    reiserfs_id_t plugin_id;

    aal_assert("umka-755", node != NULL, return NULL);
    
    plugin_id = reiserfs_node_get_item_plugin_id(node, pos);
    return libreiser4_factory_find(REISERFS_ITEM_PLUGIN, plugin_id);
}

blk_t reiserfs_node_get_pointer(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);
    aal_assert("umka-778", pos < reiserfs_node_count(node), return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
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
    
    return libreiser4_plugin_call(return 0, plugin->item.specific.internal, 
	get_pointer, body);
}

int reiserfs_node_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
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
    
    return libreiser4_plugin_call(return 0, plugin->item.specific.internal, 
	has_pointer, body, blk);
}

int reiserfs_node_item_internal(reiserfs_node_t *node, uint32_t pos) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-042", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    return libreiser4_plugin_call(return 0, plugin->item.common, internal,);
}

#ifndef ENABLE_COMPACT

void reiserfs_node_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return;
    }
    
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return;
    }
    
    libreiser4_plugin_call(return, plugin->item.specific.internal, 
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
    reiserfs_item_hint_t *hint, reiserfs_pos_t *pos)
{
    aal_assert("vpf-106", hint != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", pos != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", pos->unit != 0xffff || 
	hint->plugin != NULL, return -1);
   
    if (!hint->plugin && !(hint->plugin = 
	reiserfs_node_get_item_plugin(node, pos->item))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }

    /* Here hint has been already set for the 3rd case */
    if (hint->data != NULL)
	return 0;
    
    /* Estimate for the 2nd and for the 4th cases */
    return libreiser4_plugin_call(return -1, hint->plugin->item.common, 
	estimate, pos->item, hint);
}

void reiserfs_node_set_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_id_t plugin_id)
{
    aal_assert("umka-551", node != NULL, return);
    libreiser4_plugin_call(return, node->node_plugin->node, set_item_plugin_id, 
	node->block, pos, plugin_id);
}

#endif

