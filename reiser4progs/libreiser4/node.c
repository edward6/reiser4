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
    reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id, uint8_t level)
{
    reiserfs_node_t *node;
    
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    node->parent = parent;
    node->children = NULL;
    
    if (!(node->block = aal_device_alloc_block(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }
    
    set_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id, plugin_id);
    if (!(node->plugin = libreiser4_plugins_find_by_coords(REISERFS_NODE_PLUGIN, 
	plugin_id))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find node plugin by its identifier %x.", plugin_id);
	goto error_free_block;
    }

    if (libreiserfs_plugins_call(goto error_free_block, node->plugin->node, 
	create, node->block, level)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin hasn't been able to form a node on block %llu.", 
	    aal_device_get_block_nr(node->device, node->block));
	goto error_free_block;
    }

    return node;

error_free_block:
    aal_device_free_block(node->block);
error_free_node:
    aal_free(node);
    return NULL;
}

#endif

reiserfs_node_t *reiserfs_node_init(aal_device_t *device, blk_t blk,
    reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_node_t *node;
    
    aal_assert("umka-160", device != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    node->parent = parent;
    node->children = NULL;
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu.", blk);
	goto error_free_node;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_get_plugin_id(node);
    
    if (!(node->plugin = libreiser4_plugins_find_by_coords(REISERFS_NODE_PLUGIN, 
	plugin_id))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	goto error_free_block;
    }
    
    return node;
    
error_free_block:
    aal_device_free_block(node->block);
error_free_node:
    aal_free(node);
    return NULL;
}

void reiserfs_node_fini(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return);
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children)
	    reiserfs_node_fini((reiserfs_node_t *)walk->data);
	aal_list_free(node->children);
	node->children = NULL;
    }
    aal_device_free_block(node->block);
    aal_free(node);
}

error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    return libreiserfs_plugins_call(return -1, node->plugin->node, 
	check, node->block, flags);
}

int reiserfs_node_lookup(reiserfs_node_t *node, 
    reiserfs_item_coord_t *coord, void *key) 
{
    int found; void *body;
    reiserfs_plugin_t *item_plugin;
    
    aal_assert("umka-475", coord != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);

    if ((found = libreiserfs_plugins_call(return -1, node->plugin->node, 
	lookup, node->block, coord, key)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    if (found == 1)
	return 1;
    
    if (!(item_plugin = reiserfs_node_item_get_plugin(node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_device_get_block_nr(node->device, node->block), 
	    coord->item_pos);
	return -1;
    }
   
    if (!(body = reiserfs_node_item_at(node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_device_get_block_nr(node->device, node->block), 
	    coord->item_pos);
	return -1;
    }
    
    if (item_plugin->item.common.lookup) {
	if ((found = item_plugin->item.common.lookup(body, key)) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Lookup in the item %d in the node %llu failed.", coord->item_pos,	    
		aal_device_get_block_nr(node->device, node->block));
	    return -1;
	}
    }

    return found;
}

/* 
    Callback function for comparing two nodes by their
    left delimiting keys.
*/
static int callback_comp_for_insert(reiserfs_node_t *node1, 
    reiserfs_node_t *node2) 
{
    return reiserfs_node_item_key_cmp(node1, reiserfs_node_item_key_at(node1, 0),
	reiserfs_node_item_key_at(node1, 0));
}

/* 
    Callback function for comparing node's left delimiting key
    with given key.
*/
static int callback_comp_for_find(reiserfs_node_t *node, void *key) {
    return reiserfs_node_item_key_cmp(node, 
	reiserfs_node_item_key_at(node, 0), key);
}

/* Finds children node by its key in node cache */
reiserfs_node_t *reiserfs_node_find_child(reiserfs_node_t *node, 
    void *key)
{
    aal_list_t *list;
    
    if (!(list = aal_list_bin_search(node->children, key, 
	    (int (*)(const void *, const void *))callback_comp_for_find)))
	return NULL;

    return (reiserfs_node_t *)list->data;
}

/* Connects children into sorted list of specified node */
error_t reiserfs_node_add_children(reiserfs_node_t *node, 
    reiserfs_node_t *children) 
{
    aal_assert("umka-561", node != NULL, return -1);
    aal_assert("umka-564", children != NULL, return -1);

    node->children = aal_list_insert_sorted(node->children, 
	children, (int (*)(const void *, const void *))callback_comp_for_insert);
    
    return 0;   
}

/* Remove specified childern from node */
void reiserfs_node_remove_children(reiserfs_node_t *node, 
    reiserfs_node_t *children)
{
    uint32_t length;
    
    aal_assert("umka-562", node != NULL, return);
    aal_assert("umka-563", children != NULL, return);

    if (node->children) {
	length = aal_list_length(aal_list_first(node->children));
	aal_list_remove(node->children, node);

	if (length == 1)
	    node->children = NULL;
    }
}

#ifndef ENABLE_COMPACT

/*
    Synchronizes node's cache and frees all childrens.
    My be used when memory presure event will occur.
*/
error_t reiserfs_node_flush(reiserfs_node_t *node) {
    aal_assert("umka-575", node != NULL, return 0);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->data))
		return -1;
	}
    }
    
    if (aal_device_write_block(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %llu to device.", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }
    aal_list_free(node->children);
    node->children = NULL;
    
    aal_device_free_block(node->block);
    aal_free(node);
    return 0;
}

/* Just synchronuizes node's cache */
error_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-124", node != NULL, return 0);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children) {
	    if (reiserfs_node_sync((reiserfs_node_t *)walk->data))
		return -1;
	}
    }
    
    if (aal_device_write_block(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %llu to device.", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }
    return 0;
}

#endif

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	get_level, node->block);
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);
    
    libreiserfs_plugins_call(return, node->plugin->node, 
	set_level, node->block, level);
}

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	get_free_space, node->block);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    return libreiserfs_plugins_call(return, node->plugin->node, 
	set_free_space, node->block, value);
}

reiserfs_plugin_id_t reiserfs_node_get_plugin_id(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return get_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id);
}

void reiserfs_node_set_plugin_id(reiserfs_node_t *node, reiserfs_plugin_id_t plugin_id) {
    aal_assert("umka-603", node != NULL, return);
    set_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id, plugin_id);
}

/* Item functions */
int reiserfs_node_item_key_cmp(reiserfs_node_t *node, 
    const void *key1, const void *key2) 
{
    aal_assert("umka-568", node != NULL, return -2);
    aal_assert("umka-569", key1 != NULL, return -2);
    aal_assert("umka-570", key2 != NULL, return -2);
    
    return libreiserfs_plugins_call(return -2, node->plugin->node, 
	key_cmp, key1, key2);
}

uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	item_overhead, node->block);
}

uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	item_maxsize, node->block);
}
    
uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiserfs_plugins_call(return 0, node->plugin->node,
	item_maxnum, node->block);
}

uint16_t reiserfs_node_item_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	item_count, node->block);
}

void *reiserfs_node_item_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiserfs_plugins_call(return NULL, node->plugin->node, 
	item_at, node->block, pos);
}

void *reiserfs_node_item_key_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-565", node != NULL, return NULL);
    
    return libreiserfs_plugins_call(return NULL, node->plugin->node, 
	key_at, node->block, pos);
}

blk_t reiserfs_node_item_get_pointer(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    if (!(body = reiserfs_node_item_at(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_device_get_block_nr(node->device, node->block), pos);
	return 0;
    }
    
    return libreiserfs_plugins_call(return 0, plugin->item.specific.internal, 
	get_pointer, body);
}

void reiserfs_node_item_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return;
    }
    
    if (!(body = reiserfs_node_item_at(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_device_get_block_nr(node->device, node->block), pos);
	return;
    }
    
    libreiserfs_plugins_call(return, plugin->item.specific.internal, 
	set_pointer, body, blk);
}

int reiserfs_node_item_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return 0);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    
    if (!(body = reiserfs_node_item_at(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_device_get_block_nr(node->device, node->block), pos);
	return 0;
    }
    
    return libreiserfs_plugins_call(return 0, plugin->item.specific.internal, 
	has_pointer, body, blk);
}

int reiserfs_node_item_internal(reiserfs_node_t *node, uint32_t pos) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-042", node != NULL, return 0);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    return libreiserfs_plugins_call(return 0, plugin->item.common, internal,);
}

error_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_item_info_t *info, reiserfs_item_coord_t *coord)
{
    aal_assert("vpf-106", info != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", coord != NULL, return -1);

    if (!info->plugin && !(info->plugin = 
	reiserfs_node_item_get_plugin(node, coord->item_pos))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    if (info->data != NULL)
	return 0;

    libreiserfs_plugins_call(return -1, info->plugin->item.common, 
	estimate, info, coord);
    
    return 0;
}

error_t reiserfs_node_item_insert(reiserfs_node_t *node, 
    reiserfs_item_coord_t *coord, void *key, reiserfs_item_info_t *info) 
{
    aal_assert("vpf-108", coord != NULL, return -1);
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", info != NULL, return -1);
    aal_assert("umka-591", info->plugin != NULL, return -1);
    
    /* Estimate the size and check the free space */
    if (reiserfs_node_item_estimate(node, info, coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate space that item being inserted will consume.");
	return -1;
    }

    if (info->length + reiserfs_node_item_overhead(node) > 
	reiserfs_node_get_free_space(node)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is no space to insert the item of (%u) size in the node (%llu).", 
	    info->length, aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    return libreiserfs_plugins_call(return -1, node->plugin->node, 
	item_insert, node->block, coord, key, info); 
}

error_t reiserfs_node_item_replace(reiserfs_node_t *node, 
    reiserfs_item_coord_t *coord, void *key, reiserfs_item_info_t *info) 
{
    aal_assert("umka-598", coord != NULL, return -1);
    aal_assert("umka-599", key != NULL, return -1);
    aal_assert("umka-600", node != NULL, return -1);
    aal_assert("umka-601", info != NULL, return -1);
    aal_assert("umka-602", info->data != NULL, return -1);
    
    /* Estimate the size and check the free space */
    if (reiserfs_node_item_estimate(node, info, coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate space that item being inserted will consume.");
	return -1;
    }

    if (info->length + reiserfs_node_item_overhead(node) > 
	reiserfs_node_get_free_space(node)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is no space to insert the item of (%u) size in the node (%llu).", 
	    info->length, aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    return libreiserfs_plugins_call(return -1, node->plugin->node, 
	item_replace, node->block, coord, key, info); 
}

reiserfs_plugin_id_t reiserfs_node_item_get_plugin_id(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    return libreiserfs_plugins_call(return 0, node->plugin->node, 
	item_get_plugin_id, node->block, pos);
}

reiserfs_plugin_t *reiserfs_node_item_get_plugin(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("umka-542", node != NULL, return 0);
    return libreiser4_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, 
	reiserfs_node_item_get_plugin_id(node, pos)); 
}

void reiserfs_node_item_set_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_id_t plugin_id)
{
    aal_assert("umka-551", node != NULL, return);
    libreiserfs_plugins_call(return, node->plugin->node, item_set_plugin_id, 
	node->block, pos, plugin_id);
}

void reiserfs_node_item_set_plugin(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_t *plugin)
{
    aal_assert("umka-552", node != NULL, return);
    aal_assert("umka-553", plugin != NULL, return);
    
    reiserfs_node_item_set_plugin_id(node, pos, plugin->h.id);
}

