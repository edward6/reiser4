/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk) {
    reiserfs_node_t *node;
    reiserfs_plugin_id_t plugin_id;
    
    aal_assert("umka-160", device != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    
    if (!(node->block = aal_device_read_block(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read block %d.", blk);
	goto error_free_node;
    }
    
    plugin_id = reiserfs_node_get_plugin_id(node->block);
    if (!(node->plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Node plugin cannot be find by its identifier %x.", plugin_id);
	goto error_free_block;
    }
    
    return node;

error_free_block:
    aal_device_free_block(node->block);
error_free_node:
    aal_free (node);
    return NULL;
}

/* 
    Parameters:
    (1) device a node will be created on
    (2) allocated block
    (3) node plugin id to be used
    (4) level of the node in the tree 
*/
reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk, 
    reiserfs_plugin_id_t plugin_id, uint8_t level)
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
	    
    reiserfs_node_set_plugin_id(node->block, plugin_id);
    if (!(node->plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find node plugin by its identifier %x.", plugin_id);
	goto error_free_node;
    }
    
    reiserfs_plugin_check_routine(node->plugin->node, create, goto error_free_node);
    if (node->plugin->node.create(node->block, level)) {
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

void reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return);
    
    aal_device_free_block(node->block);
    aal_free(node);
}

/* Returns "true" on success or "false" on failure */
error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);

    reiserfs_plugin_check_routine(node->plugin->node, check, return -1);
    return node->plugin->node.check(node->block, flags);
}

/* Syncs formed node onto device */
error_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-124", node != NULL, return 0);
    
    if (!aal_device_write_block(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %d to device.", 
	    aal_device_get_block_location(node->device, node->block));
	return -1;
    }
    return 0;
}

/* 
    Here must be more smart code in order to guess 
    what node format is used. This code must be aware
    about reiser3 node format.
*/
reiserfs_plugin_id_t reiserfs_node_get_plugin_id(aal_block_t *block) {
    aal_assert("umka-161", block != NULL, return -1);
    return get_le16((reiserfs_node_common_header_t *)block->data, plugin_id);
}

void reiserfs_node_set_plugin_id(aal_block_t *block, reiserfs_plugin_id_t id) {
    aal_assert("umka-162", block != NULL, return);
    set_le16((reiserfs_node_common_header_t *)block->data, plugin_id, id);
}

uint32_t reiserfs_node_max_item_size(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    reiserfs_plugin_check_routine(node->plugin->node, max_item_size, return 0); 
    return node->plugin->node.max_item_size(node->block);
}
    
uint32_t reiserfs_node_max_item_num(reiserfs_node_t *node) {
    return 0;
}

uint32_t reiserfs_node_count(reiserfs_node_t *node) {
    return 0;
}

uint8_t reiserfs_node_level(reiserfs_node_t *node) {
    reiserfs_plugin_check_routine(node->plugin->node, level, return 0);
    return node->plugin->node.level(node->block);
}

uint32_t reiserfs_node_free_space(reiserfs_node_t *node) {
    return 0;
}

void reiserfs_node_set_free_space(reiserfs_node_t *node) {
}
