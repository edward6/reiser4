/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiserfs/debug.h>
#include "node40.h"

static error_t reiserfs_node40_confirm (aal_device_block_t *block);
static error_t reiserfs_node40_create(aal_device_block_t *block, uint8_t level);
static error_t reiserfs_node40_check(aal_device_block_t *block, int flags);
static error_t reiserfs_node40_sync(aal_device_block_t *block);
static uint32_t reiserfs_node40_max_item_size(aal_device_block_t *block); 
static uint32_t reiserfs_node40_max_item_num(aal_device_block_t *block); 
static uint32_t reiserfs_node40_count(aal_device_block_t *block);
static uint8_t reiserfs_node40_level(aal_device_block_t *block);
static uint32_t reiserfs_node40_free_space(aal_device_block_t *block); 
static uint32_t reiserfs_node40_get_free_space(aal_device_block_t *block); 
static void reiserfs_node40_set_free_space(aal_device_block_t *block, uint32_t free_space); 
static void reiserfs_node40_print (aal_device_block_t *block);

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

	.confirm_format = (error_t (*)(aal_device_block_t *))reiserfs_node40_confirm,
	.create = (error_t (*)(aal_device_block_t *, uint8_t))reiserfs_node40_create,
	.check =  (error_t (*)(aal_device_block_t *, int))reiserfs_node40_check,
	.sync  =  (error_t (*)(aal_device_block_t *))reiserfs_node40_sync,
	.max_item_size = (uint32_t (*)(aal_device_block_t *))reiserfs_node40_max_item_size,
	.max_item_num =  (uint32_t (*)(aal_device_block_t *))reiserfs_node40_max_item_num,
	.count = (uint32_t (*)(aal_device_block_t *))reiserfs_node40_count,
	.level = (uint8_t (*)(aal_device_block_t *))reiserfs_node40_level,
	.get_free_space = (uint32_t(*)(aal_device_block_t *))reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(aal_device_block_t *, uint32_t))reiserfs_node40_set_free_space,
	.print = (void (*)(aal_device_block_t *))reiserfs_node40_print
    }
};

static error_t reiserfs_node40_confirm (aal_device_block_t *block) {
    ASSERT(block != NULL, return -1);
    ASSERT(block->data != NULL, return -1);
    
    if (get_nh40_magic(node_header(block)) != reiser4_node_magic) 
	return 1;    
 
    return 0;
}

static error_t reiserfs_node40_create(aal_device_block_t *block, uint8_t level) {
    ASSERT(block != NULL, return -1);
    ASSERT(block->data != NULL, return -1);
    
    aal_memset (node_header(block), 0, sizeof (reiserfs_node40_header_t));
    reiserfs_node_set_plugin_id (block, node40_plugin.h.id);
    set_nh40_free_space (node_header(block), aal_block_get_size (block)-sizeof(reiserfs_node40_header_t));
    set_nh40_free_space_start (node_header(block), aal_block_get_size (block));
    set_nh40_level (node_header(block), level);
    set_nh40_magic (node_header(block), reiser4_node_magic);
     
    return 0;
}

static error_t reiserfs_node40_check(aal_device_block_t *block, int flags) {
    ASSERT(block != NULL, return -1);
    ASSERT(block->data != NULL, return -1);
 
    if (get_nh40_magic(node_header(block)) != reiser4_node_magic) 
	return -1;    
 
    return 0;
}

static error_t reiserfs_node40_sync(aal_device_block_t *block) {
    ASSERT(block != NULL, return -1);
    ASSERT(block->data != NULL, return -1);
    
    return aal_device_write_block (block->device, block);
}

static uint32_t reiserfs_node40_max_item_size(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);
 
    return 0;
}

static uint32_t reiserfs_node40_max_item_num(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);
    return 0;
}

static uint32_t reiserfs_node40_count(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);

    return get_nh40_num_items (node_header(block));
}

static uint8_t reiserfs_node40_level(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);

    return get_nh40_level (node_header(block));
}

static uint32_t reiserfs_node40_free_space(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);

    return get_nh40_level (node_header(block));
}

static uint32_t reiserfs_node40_get_free_space(aal_device_block_t *block) {
    ASSERT(block != NULL, return 0);
    ASSERT(block->data != NULL, return 0);

    return get_nh40_free_space (node_header(block));
}

static void reiserfs_node40_set_free_space(aal_device_block_t *block, uint32_t free_space) {
    ASSERT(block != NULL, return);
    ASSERT(block->data != NULL, return);

    set_nh40_free_space (node_header(block), free_space);
}

static aal_device_block_t *reiserfs_node40_block(aal_device_block_t *block) {
    ASSERT(block != NULL, return NULL);
    ASSERT(block->data != NULL, return NULL);
    return NULL;
}

static void reiserfs_node40_print (aal_device_block_t *block) {
    ASSERT(block != NULL, return);
    ASSERT(block->data != NULL, return);
}

reiserfs_plugin_register (node40_plugin);
