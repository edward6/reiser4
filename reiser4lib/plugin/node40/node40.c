#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "node40.h"


static reiserfs_node40_t *reiserfs_node40_open(aal_device_block_t *block) {
    return NULL;
}

static reiserfs_node40_t *reiserfs_node40_create(aal_device_block_t *block, uint8_t level) {
    return NULL;
}

static int reiserfs_node40_check(reiserfs_node40_t *node, int flags) {
    return 0;
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

reiserfs_plugin_t plugin_info = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "NODE40",
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

reiserfs_plugin_t *reiserfs_plugin_info() {
    return &plugin_info;
}

