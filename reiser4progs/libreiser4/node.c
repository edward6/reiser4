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

/*
    Vitaly, at the moment architecture of libreiser4 doesn't mean
    write anything to device out of "sync" method. So, writing created 
    node in this method is not valid. Node writing should be performed
    in "sync" method of the node.
*/
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

    reiserfs_check_method(node->plugin->node, create, goto error_free_block);
    if (node->plugin->node.create(node->block, level)) {
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
    
    /*
	FIXME-UMKA: Here we need to check whether given blk
	is allocated already.
    */
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu.", blk);
	goto error_free_node;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_plugin_id(node);
    
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

/*
    FIXME-UMKA: Probably here we should check also every node's item
    by its check method.
*/
error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    reiserfs_check_method(node->plugin->node, check, return -1);
    return node->plugin->node.check(node->block, flags);
}

int reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_item_coord_t *coord, 
    reiserfs_key_t *key) 
{
    int found; void *body;
    reiserfs_plugin_t *item_plugin;
    
    aal_assert("umka-475", coord != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);

    reiserfs_check_method(node->plugin->node, lookup, return -1);
    if ((found = node->plugin->node.lookup(node->block, coord, key)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    if (found == 1)
	return 1;
    
    if (!(item_plugin = reiserfs_node_get_item_plugin(node, coord->item_pos))) {
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
    
    /*
	Apparently we need lookup method in item API. And here we might call
	them for lookup purposes. It might make all needed checks.
    */
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

/* Finds children node by its key in node cache */
reiserfs_node_t *reiserfs_node_find_child(reiserfs_node_t *node, 
    void *key)
{
    return NULL;
}

/*
    As reiser3 has another format of keys, we need node's plugin
    function for comparing two keys. However some problems remain.
    This is what is happening when we will need to compare two
    keys of different format?
*/
int reiserfs_node_item_key_cmp(reiserfs_node_t *node, 
    const void *key1, const void *key2) 
{
    aal_assert("umka-568", node != NULL, return -2);
    aal_assert("umka-569", key1 != NULL, return -2);
    aal_assert("umka-570", key2 != NULL, return -2);
    
    reiserfs_check_method(node->plugin->node, key_cmp, return -2);
    return node->plugin->node.key_cmp(key1, key2);
}

/* 
    Callback function for compatring two nodes by their
    left delimiting keys. It is used by aal_list_insert_sorted
    function.
*/
static int callback_comp_left_keys(const void *n1, const void *n2) {
    const void *key1, *key2;
    reiserfs_node_t *node1 = (reiserfs_node_t *)n1;
    reiserfs_node_t *node2 = (reiserfs_node_t *)n2;
   
    reiserfs_check_method(node1->plugin->node, confirm, return -2);

    /* Check whether two given nodes have the same format */
    aal_assert("umka-571", node1->plugin->node.confirm(node2->block), return -2);

    key1 = reiserfs_node_item_key_at(node1, 0);
    key2 = reiserfs_node_item_key_at(node2, 0);
    
    return reiserfs_node_item_key_cmp(node1, key1, key2);
}

/* Connects children into sorted list of specified node */
error_t reiserfs_node_add_children(reiserfs_node_t *node, 
    reiserfs_node_t *children) 
{
    aal_assert("umka-561", node != NULL, return -1);
    aal_assert("umka-564", children != NULL, return -1);

    node->children = aal_list_insert_sorted(node->children, 
	children, callback_comp_left_keys);
    
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

reiserfs_plugin_id_t reiserfs_node_plugin_id(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return get_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id);
}

uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    reiserfs_check_method(node->plugin->node, item_overhead, return 0);
    return node->plugin->node.item_overhead(node->block);
}

uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_maxsize, return 0); 
    return node->plugin->node.item_maxsize(node->block);
}
    
uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_maxnum, return 0); 
    return node->plugin->node.item_maxnum(node->block);
}

uint16_t reiserfs_node_item_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_count, return 0); 
    return node->plugin->node.item_count(node->block);
}

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->plugin->node.get_level == NULL)
	return 0; 

    return node->plugin->node.get_level(node->block);
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);
    
    if (node->plugin->node.set_level == NULL)
	return; 

    node->plugin->node.set_level(node->block, level);
}

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, get_free_space, return 0); 
    return node->plugin->node.get_free_space(node->block);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    reiserfs_check_method(node->plugin->node, set_free_space, return); 
    return node->plugin->node.set_free_space(node->block, value);
}

void *reiserfs_node_item_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    reiserfs_check_method(node->plugin->node, item_at, return NULL);
    return node->plugin->node.item_at(node->block, pos);
}

void *reiserfs_node_item_key_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-565", node != NULL, return NULL);
    
    reiserfs_check_method(node->plugin->node, key_at, return NULL);
    return node->plugin->node.item_at(node->block, pos);
}

blk_t reiserfs_node_down_link_item(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    if (plugin->item.specific.internal.down_link == NULL)
	return 0;
    
    if (!(body = reiserfs_node_item_at(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_device_get_block_nr(node->device, node->block), pos);
	return 0;
    }
    
    return plugin->item.specific.internal.down_link(body);
}

int reiserfs_node_is_internal_item(reiserfs_node_t *node, uint32_t pos) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-042", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    reiserfs_check_method(plugin->item.common, is_internal, return 0);
    return plugin->item.common.is_internal();
}

error_t reiserfs_node_estimate_item(reiserfs_node_t *node, 
    reiserfs_item_info_t *item_info, reiserfs_item_coord_t *coord)
{
    aal_assert("vpf-106", item_info != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);

    if (!item_info->plugin && !(item_info->plugin = 
	reiserfs_node_get_item_plugin(node, coord->item_pos))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    if (item_info->data != NULL)
	return 0;

    reiserfs_check_method(item_info->plugin->item.common, estimate, return -1);    
    item_info->plugin->item.common.estimate(item_info, coord);
    
    return 0;
}

error_t reiserfs_node_insert_item(reiserfs_node_t *node, reiserfs_item_coord_t *coord, 
    reiserfs_key_t *key, reiserfs_item_info_t *item_info) 
{
    void *body;
    
    aal_assert("vpf-108", coord != NULL, return -1);
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", item_info != NULL, return -1);
    
    reiserfs_check_method(node->plugin->node, insert, return -1);
    
    /* Estimate the size and check the free space */
    if (reiserfs_node_estimate_item(node, item_info, coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate space that item being inserted will consume.");
	return -1;
    }

    if (item_info->length + reiserfs_node_item_overhead(node) > 
	reiserfs_node_get_free_space(node)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is no space to insert the item of (%u) size in the node (%llu).", 
	    item_info->length, aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    if (node->plugin->node.insert(node->block, coord, key, item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an item into the node %llu.", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    if (!(body = reiserfs_node_item_at(node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u", 
	    aal_device_get_block_nr(node->device, node->block), coord->item_pos);
	return -1;
    }
    
    /* Insert item or create it if needed */
    if (item_info->plugin == NULL)
    	aal_memcpy(body, item_info->data, item_info->length);
    else {
	reiserfs_check_method(item_info->plugin->item.common, create, return -1);
	if (item_info->plugin->item.common.create(body, item_info)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Item plugin could not create an item (%d) in the node (%llu).", 
		coord->item_pos, aal_device_get_block_nr(node->device, node->block));
	    return -1;
	}
    }
    
    return 0;
}

reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, get_item_plugin_id, return 0);
    return node->plugin->node.get_item_plugin_id(node->block, pos);
}

reiserfs_plugin_t *reiserfs_node_get_item_plugin(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("umka-542", node != NULL, return 0);
    return libreiser4_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, 
	reiserfs_node_get_item_plugin_id(node, pos)); 
}

void reiserfs_node_set_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_id_t plugin_id)
{
    aal_assert("umka-551", node != NULL, return);
    
    reiserfs_check_method(node->plugin->node, set_item_plugin_id, return);
    node->plugin->node.set_item_plugin_id(node->block, pos, plugin_id);
}

void reiserfs_node_set_item_plugin(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_t *plugin)
{
    aal_assert("umka-552", node != NULL, return);
    aal_assert("umka-553", plugin != NULL, return);
    
    reiserfs_node_set_item_plugin_id(node, pos, plugin->h.id);
}

