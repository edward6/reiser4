/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/debug.h>
#include "node40.h"

static reiserfs_node40_t *reiserfs_node40_open(aal_device_block_t *block);
static reiserfs_node40_t *reiserfs_node40_create(aal_device_block_t *block, uint8_t level);
static int reiserfs_node40_check(reiserfs_node40_t *node, int flags);
static void reiserfs_node40_close(reiserfs_node40_t *node, int sync);
static int reiserfs_node40_sync(reiserfs_node40_t *node);
static uint32_t reiserfs_node40_max_item_size(reiserfs_node40_t *node); 
static uint32_t reiserfs_node40_max_item_num(reiserfs_node40_t *node); 
static uint32_t reiserfs_node40_count(reiserfs_node40_t *node);
static uint8_t reiserfs_node40_level(reiserfs_node40_t *node);
static uint32_t reiserfs_node40_free_space(reiserfs_node40_t *node); 
static uint32_t reiserfs_node40_set_free_space(reiserfs_node40_t *node); 
static void reiserfs_node40_get_free_space(reiserfs_node40_t *node, uint32_t free_space); 
static aal_device_block_t *reiserfs_node40_block(reiserfs_node40_t *node); 
static void reiserfs_node40_print (reiserfs_node40_t *node);

static reiserfs_plugin_t node40_plugin = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "Node40",
	    .desc = "Node for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},

	.open =   (reiserfs_node_opaque_t *(*)(aal_device_block_t *))reiserfs_node40_open,
	.create = (reiserfs_node_opaque_t *(*)(aal_device_block_t *, uint8_t))reiserfs_node40_create,
	.close =  (void (*)(reiserfs_node_opaque_t *, int))reiserfs_node40_close,
	.check =  (int (*)(reiserfs_node_opaque_t *, int))reiserfs_node40_check,
	.sync  =  (int (*)(reiserfs_node_opaque_t *))reiserfs_node40_sync,
	.max_item_size = (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_max_item_size,
	.max_item_num =  (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_max_item_num,
	.count = (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_count,
	.level = (uint8_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_level,
	.block = (aal_device_block_t *(*)(reiserfs_node_opaque_t *))reiserfs_node40_block,
	.get_free_space = (uint32_t(*)(reiserfs_node_opaque_t *))reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(reiserfs_node_opaque_t *, uint32_t))reiserfs_node40_set_free_space,
	.print = (void (*)(reiserfs_node_opaque_t *))reiserfs_node40_print
    }
};

static reiserfs_node40_t *reiserfs_node40_open(aal_device_block_t *block) {
    reiserfs_node40_t * node;
    
    ASSERT(block != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;

    return node;
}

static reiserfs_node40_t *reiserfs_node40_create(aal_device_block_t *block, uint8_t level) {
    reiserfs_node40_t * node;
    reiserfs_node40_header_t * node_header;
    
    /* untill open does not do any special check, we can just open the node 
     * and set default values there */
    if ((node = reiserfs_node40_open (block)) == NULL)
	return NULL;
    
    node_header = (reiserfs_node40_header_t *)block->data;
    
    aal_memset (node_header, 0, sizeof (*node_header));
    reiserfs_node_set_plugin_id (block, node40_plugin.h.id);
    set_node_level (node_header, level);
    set_node_magic (node_header, reiser4_node_magic);
    set_node_free_space_start (node_header, aal_block_get_size (block));
     
    return node;
}

static int reiserfs_node40_check(reiserfs_node40_t *node, int flags) {
    reiserfs_node40_header_t * node_header;

    node_header = (reiserfs_node40_header_t *)node->block->data;

    if (get_node_magic(node_header) != reiser4_node_magic) 
	return 0;    
    
    return 1;
}

static void reiserfs_node40_close(reiserfs_node40_t *node, int sync) {
}

static int reiserfs_node40_sync(reiserfs_node40_t *node) {
    return 0;

}

static uint32_t reiserfs_node40_max_item_size(reiserfs_node40_t *node) {
    return 0;

}

static uint32_t reiserfs_node40_max_item_num(reiserfs_node40_t *node) {
    return 0;

}

static uint32_t reiserfs_node40_count(reiserfs_node40_t *node) {
    return 0;

}

static uint8_t reiserfs_node40_level(reiserfs_node40_t *node) {
    return 0;

}

static uint32_t reiserfs_node40_free_space(reiserfs_node40_t *node) {
    return 0;

}

static uint32_t reiserfs_node40_set_free_space(reiserfs_node40_t *node) {
    return 0;
}

static void reiserfs_node40_get_free_space(reiserfs_node40_t *node, uint32_t free_space) {
}

static aal_device_block_t *reiserfs_node40_block(reiserfs_node40_t *node) {
    return NULL;
}

static void reiserfs_node40_print (reiserfs_node40_t *node) {
}

reiserfs_plugin_register (node40_plugin);
