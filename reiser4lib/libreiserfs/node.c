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
    reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_node_t *node;
    
    aal_assert("umka-160", device != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read block %d.", blk);
	goto error_free_node;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_plugin(node);
    
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	goto error_free_block;
    }
    
    reiserfs_plugin_check_routine(node->plugin->node, open, return NULL);
    
    if (!(node->entity = node->plugin->node.open (node->device, node->block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,		
	    "Node plugin hasn't been able to open a node %d.", 
            aal_device_get_block_location (node->device, node->block));
       goto error_free_node;
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
    reiserfs_plugin_id_t plugin_id,		/* node plugin id to be used */
    uint8_t level)				/* level of the node in the tree */

{
    reiserfs_node_t *node;
 
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    
    if (!(node->block = aal_device_alloc_block(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate block %d.", blk);
	goto error_free_node;
    }
	    
    set_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id, plugin_id);
    if (!(node->plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find node plugin by its identifier %x.", plugin_id);
	goto error_free_node;
    }
    
    reiserfs_plugin_check_routine(node->plugin->node, create, goto error_free_node);
    if (!(node->entity = node->plugin->node.create(node->device, node->block, level))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin hasn't been able to create a node on block %d.", blk);
	goto error_free_node;
    }

    return node;

error_free_node:
    aal_free (node);
error:    
    return NULL;
}

#endif

void reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return);
    
    aal_device_free_block(node->entity);
    aal_free(node);
}

error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);

    reiserfs_plugin_check_routine(node->plugin->node, check, return -1);
    return node->plugin->node.check(node->entity, flags);
}

reiserfs_coord_t *reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key) {
    reiserfs_coord_t *coord;
    
    aal_assert("umka-475", node != NULL, return NULL);
    aal_assert("umka-476", key != NULL, return NULL);

    if (!(coord = aal_calloc(sizeof(*coord), 0)))
	return NULL;

    reiserfs_plugin_check_routine(node->plugin->node, lookup, return NULL);
    node->plugin->node.lookup(node->entity, key, coord);

    return coord;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-124", node != NULL, return 0);
    
    if (aal_device_write_block(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %d to device.", 
	    aal_device_get_block_location(node->device, node->block));
	return -1;
    }
    return 0;
}

#endif

reiserfs_plugin_id_t reiserfs_node_plugin(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return get_le16((reiserfs_node_common_header_t *)node->block->data, plugin_id);
}

uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, item_maxsize, return 0); 
    return node->plugin->node.item_maxsize(node->entity);
}
    
uint32_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, item_maxnum, return 0); 
    return node->plugin->node.item_maxnum(node->entity);
}

uint32_t reiserfs_node_item_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, item_count, return 0); 
    return node->plugin->node.item_count(node->entity);
}

uint8_t reiserfs_node_level(reiserfs_node_t *node) {
    aal_assert("umka-454", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, level, return 0); 
    return node->plugin->node.level(node->entity);
}

uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, get_free_space, return 0); 
    return node->plugin->node.get_free_space(node->entity);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    reiserfs_plugin_check_routine(node->plugin->node, set_free_space, return); 
    return node->plugin->node.set_free_space(node->entity, value);
}

