/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

reiserfs_node_t *reiserfs_node_alloc() {
    reiserfs_node_t *node;

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    return node;
}

error_t reiserfs_node_free (reiserfs_node_t *node) {
    aal_assert("vpf-057", node != NULL, return -1);

    aal_free(node);
    return 0;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_node_create(reiserfs_node_t *node, aal_device_t *device,
    blk_t blk, reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id, uint8_t level)
{
    int no_node = 0;
    reiserfs_node_t *work_node;
    
    /* node could be NULL */ 
    aal_assert("umka-121", device != NULL, return -1);

    if (node == NULL) {
	no_node = 1;
	if (!(work_node = reiserfs_node_alloc())) {
	   aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't allocate a node %llu.", blk);
	    return -1; 
	}
    } else 
	work_node = node;
   
    work_node->device = device;
    work_node->parent = parent;
    
    if (!(work_node->block = aal_device_alloc_block(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }

    set_le16((reiserfs_node_common_header_t *)work_node->block->data, plugin_id, plugin_id);
    if (!(work_node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, 
	plugin_id))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find node plugin by its identifier %x.", plugin_id);
	goto error_free_block;
    }

    if (work_node->plugin->node.create != NULL ) {
	if (work_node->plugin->node.create(work_node, level)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Node plugin hasn't been able to create a node on block %llu.", blk);
	    goto error_free_block;
	}
    }

    if (aal_device_write_block(device, work_node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write block %llu on device.", blk);
	goto error_free_block;
    }
   
    if (no_node)
	aal_free(work_node);
    
    return 0;

error_free_block:
    aal_device_free_block(work_node->block);
error_free_node:
    if (no_node)
	aal_free(work_node);

    return -1;
}

#endif

error_t reiserfs_node_open(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk, reiserfs_node_t *parent, reiserfs_plugin_id_t plugin_id) 
{
    aal_assert("vpf-058", node != NULL, return -1);
    aal_assert("umka-160", device != NULL, return -1);
    
    node->device = device;
    node->parent = parent;
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read block %llu.", blk);
	return -1;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_plugin_id(node);
    
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, 
	plugin_id))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	goto error_free_block;
    }
    
    if (node->plugin->node.open != NULL) {
	if (node->plugin->node.open (node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,		
		"Node plugin hasn't been able to open a node %llu.", 
		aal_device_get_block_nr(node->device, node->block));
	    goto error_free_block;
	}
    }
    
    return 0;

error_free_block:
    aal_device_free_block(node->block);
    return -1;
}

error_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return -1);

    if (!node->plugin) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't close the node (%llu).", 
	    aal_device_get_block_nr(node->device, node->block));
	return -1;
    }

    if (node->plugin->node.close != NULL) {
	if (node->plugin->node.close (node)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't close node (%llu).",
		aal_device_get_block_nr(node->device, node->block));
	    return -1;
	}
    }
   
    aal_device_free_block(node->block);
    return 0;
}

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

    /* 
	We need to lookup in the found item. Check that the key is in 
	item first 
    */
    item.coord = coord;
    if (reiserfs_item_open(&item)) {
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

uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    reiserfs_check_method(node->plugin->node, item_overhead, return 0);
    return node->plugin->node.item_overhead(node);
}

uint16_t reiserfs_node_item_max_size(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_max_size, return 0); 
    return node->plugin->node.item_max_size(node);
}
    
uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_max_num, return 0); 
    return node->plugin->node.item_max_num(node);
}

uint16_t reiserfs_node_item_count(reiserfs_node_t *node) {
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

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
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
    reiserfs_item_info_t *item_info, reiserfs_plugin_id_t id) 
{
    aal_assert("vpf-108", coord != NULL, return -1);
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("vpf-110", item_info != NULL, return -1);
    aal_assert("vpf-111", coord->node != NULL, return -1);
    
    reiserfs_check_method (coord->node->plugin->node, insert, return -1);
    
    /* Estimate the size and check the free space */
    if (reiserfs_item_estimate (coord, item_info, id)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't estimate space that item being inserted will consume.");
	return -1;
    }

    if (item_info->length + reiserfs_node_item_overhead (coord->node) > 
	reiserfs_node_get_free_space(coord->node)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "There is no space to insert the item of (%u) size in the node (%llu).", 
	    item_info->length, 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }
    
    if (coord->node->plugin->node.insert(coord, key, item_info)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't insert an item into the node %llu.", 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }

    /* Insert item or create it if needed */
    if (item_info->plugin == NULL) {
	reiserfs_check_method (coord->node->plugin->node, item, return -1);
    
	aal_memcpy(coord->node->plugin->node.item(coord->node, coord->item_pos), 
	    item_info->data, item_info->length);
    } else {
	reiserfs_check_method (item_info->plugin->item.common, create, return -1);
	if (item_info->plugin->item.common.create (coord, item_info)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Item plugin could not create an item (%d) in the node (%llu).", 
		coord->item_pos,
		aal_device_get_block_nr(coord->node->device, coord->node->block));
	    return -1;
	}
    } 
    
    return 0;
}

reiserfs_plugin_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, 
    uint16_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    
    reiserfs_check_method(node->plugin->node, item_plugin_id, return 0);
    return node->plugin->node.item_plugin_id(node, pos);
}

