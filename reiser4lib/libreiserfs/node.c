/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_node_t *node;
    
    aal_assert("umka-160", device != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    node->parent = parent;
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read block %llu.", blk);
	goto error_free_node;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_plugin_id(node);
    
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	goto error_free_block;
    }
    
    if (node->plugin->node.open != NULL) {
	if (node->plugin->node.open (node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,		
		"Node plugin hasn't been able to open a node %llu.", 
		aal_device_get_block_nr(node->device, node->block));
	    goto error_free_node;
	}
    }
    
    return node;

error_free_block:
    aal_device_free_block(node->block);
error_free_node:
    aal_free (node);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_node_t *reiserfs_node_create(
    aal_device_t *device,			/* device which a node will be created on */
    blk_t blk,					/* allocated block */
    reiserfs_node_t *parent,
    reiserfs_plugin_id_t plugin_id,		/* node plugin id to be used */
    uint8_t level)				/* level of the node in the tree */

{
    reiserfs_node_t *node;
 
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    node->parent = parent;
    
    if (!(node->block = aal_device_alloc_block(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }
	    
    set_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id, plugin_id);
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, 
	plugin_id))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find node plugin by its identifier %x.", plugin_id);
	goto error_free_block;
    }

    if (node->plugin->node.create != NULL ) {
	if (node->plugin->node.create(node, level)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Node plugin hasn't been able to create a node on block %llu.", blk);
	    goto error_free_block;
	}
    }
    
    return node;

error_free_block:
    aal_device_free_block(node->block);
error_free_node:
    aal_free (node);
error:    
    return NULL;
}

#endif

error_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return -1);

    if (reiserfs_node_fini(node)) 
	return -1;
    
    aal_device_free_block(node->block);
    aal_free(node);
    return 0;
}

error_t reiserfs_node_fini(reiserfs_node_t *node) {
   aal_assert("vpf-053", node != NULL, return -1);
   
    if (node->plugin->node.close != NULL) {
	if (node->plugin->node.close (node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't close node.");
	    return -1;
	}
    } 
    return 0;
}

error_t reiserfs_node_init(reiserfs_node_t *node, reiserfs_node_t *parent, 
    aal_device_t *device, aal_block_t *block, reiserfs_plugin_id_t plugin_id) 
{
    aal_assert("vpf-049", node != NULL, return -1);
    aal_assert("vpf-054", node->block != NULL || block != NULL, return -1);
    if (parent != NULL)
	node->parent = parent;
    if (device != NULL)
	node->device = device;
    if (block != NULL)
	node->block = block;

    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_plugin_id(node);
    
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	return -1;
    }
    
    if (node->plugin->node.open != NULL) {
	if (node->plugin->node.open (node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,		
		"Node plugin hasn't been able to open a node %llu.", 
		aal_device_get_block_nr(node->device, node->block));
	    return -1;
	}
    }
    
    return 0;
}

/*
error_t reiserfs_node_add(reiserfs_node_t *node, reiserfs_node_t *child) {
    aal_assert("umka-480", node != NULL, return -1);
    aal_assert("umka-481", child != NULL, return -1);

    child->parent = node;
    return 0;
}
*/

error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);

    reiserfs_check_method(node->plugin->node, check, return -1);
    return node->plugin->node.check(node, flags);
}

int reiserfs_node_lookup(reiserfs_coord_t *coord, reiserfs_key_t *key) {
    reiserfs_item_t item;
    int found;
    
    aal_assert("umka-475", coord != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);
    aal_assert("vpf-048", coord->node != NULL, return -1);

    reiserfs_check_method(coord->node->plugin->node, lookup, return -1);
    if ((found = coord->node->plugin->node.lookup(coord, key)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }

    if (found == 1)
	return 1;

    /* We need to lookup in the found item. Check that the key is in 
       item first */
    if (reiserfs_item_init (&item, coord)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Item %d in node %llu initialization failed.", coord->item_pos,
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }

    reiserfs_check_method (item.plugin->item.common, lookup, return -1);
    if ((found = item.plugin->item.common.lookup (&item, key)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", coord->item_pos,	    
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }

    return found;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-124", node != NULL, return 0);
    
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

uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_max_size, return 0); 
    return node->plugin->node.item_max_size(node);
}
    
uint32_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_max_num, return 0); 
    return node->plugin->node.item_max_num(node);
}

uint32_t reiserfs_node_item_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_count, return 0); 
    return node->plugin->node.item_count(node);
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);
    
    if (node->plugin->node.set_level == NULL)
	return; 

    node->plugin->node.set_level(node, level);
}

uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, get_free_space, return 0); 
    return node->plugin->node.get_free_space(node);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    reiserfs_check_method(node->plugin->node, set_free_space, return); 
    return node->plugin->node.set_free_space(node, value);
}

void *reiserfs_node_item(reiserfs_node_t *node, uint32_t pos) {
    return NULL;
}

int reiserfs_node_insert_item(reiserfs_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_info_t *item) 
{
    reiserfs_check_method (coord->node->plugin->node, insert, return -1);
    
    if (coord->node->plugin->node.insert (&coord, &key, &item)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an item into the node %llu.", 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }
    return 0;
}

reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, uint16_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_plugin_id, return 0);
    return node->plugin->node.item_plugin_id(node, pos);
}
