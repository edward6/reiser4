/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "node40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static void *reiserfs_node40_key_at(void *node, int64_t pos) {
    aal_assert("vpf-009", node != NULL, return NULL);
    return &(reiserfs_node40_ih_at(node, pos)->key);
}

/* start_position and end_position must point to the same node */
static error_t reiserfs_node40_remove(reiserfs_coord_t *start_position, 
    reiserfs_coord_t *end_position) 
{
    aal_assert("vpf-010", start_position != NULL, return -1);
    aal_assert("vpf-024", end_position != NULL, return -1);
    aal_assert("vpf-025", reiserfs_node40_block(start_position->node) != NULL, return -1);
    aal_assert("vpf-028", reiserfs_node40_block(end_position) != NULL, return -1);
    
    return 0;
}

static error_t reiserfs_node40_move(reiserfs_node40_t *node_dest, reiserfs_node40_t *node_dst,
    reiserfs_key_t *dest_key, reiserfs_key_t *src_key_from, reiserfs_key_t *src_key_to) 
{
    return 0;
}

static reiserfs_node40_t *reiserfs_node40_open(aal_device_t *device, aal_block_t *block) {
    reiserfs_node40_t * node;
    
    aal_assert("vpf-011", device != NULL, return NULL);
    aal_assert("vpf-008", block != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;
    node->device = device;

    return node;
}

static reiserfs_node40_t *reiserfs_node40_create(aal_device_t *device, aal_block_t *block, 
    uint8_t level) 
{
    reiserfs_node40_t *node;
    
    aal_assert("vpf-012", device != NULL, return NULL);
    aal_assert("vpf-013", block != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;
    node->device = device;

    aal_memset(reiserfs_node40_header(node), 0, sizeof(reiserfs_node40_header_t));

    set_nh40_free_space(reiserfs_node40_header(node), 
	block->size - sizeof(reiserfs_node40_header_t));
    
    set_nh40_free_space_start(reiserfs_node40_header(node), block->size);
    set_nh40_level(reiserfs_node40_header(node), level);
    set_nh40_magic(reiserfs_node40_header(node), reiserfs_node_magic);

    return node;
}

static error_t reiserfs_node40_confirm(reiserfs_node40_t *node) {
    aal_assert("vpf-014", node != NULL, return -1);
    
    return -(get_nh40_magic(reiserfs_node40_header(node)) != reiserfs_node_magic);
}

static error_t reiserfs_node40_check(reiserfs_node40_t *node, int flags) {
    aal_assert("vpf-015", node != NULL, return -1);
 
    if (get_nh40_magic(reiserfs_node40_header(node)) != reiserfs_node_magic) 
	return -1;
 
    return 0;
}

static uint32_t reiserfs_node40_max_item_size(reiserfs_node40_t *node) {
    aal_assert("vpf-016", node != NULL, return 0);
 
    return 0;
}

static uint32_t reiserfs_node40_max_item_num(reiserfs_node40_t *node) {
    aal_assert("vpf-017", node != NULL, return 0);
    
    return 0;
}

static uint32_t reiserfs_node40_item_count(reiserfs_node40_t *node) {
    aal_assert("vpf-018", node != NULL, return 0);

    return get_nh40_num_items(reiserfs_node40_header(node));
}

static uint8_t reiserfs_node40_level(reiserfs_node40_t *node) {
    aal_assert("vpf-019", node != NULL, return 0);

    return get_nh40_level(reiserfs_node40_header(node));
}

static uint32_t reiserfs_node40_free_space(reiserfs_node40_t *node) {
    aal_assert("vpf-020", node != NULL, return 0);

    return get_nh40_level(reiserfs_node40_header(node));
}

static uint32_t reiserfs_node40_get_free_space(reiserfs_node40_t *node) {
    aal_assert("vpf-021", node != NULL, return 0);

    return get_nh40_free_space(reiserfs_node40_header(node));
}

static void reiserfs_node40_set_free_space(reiserfs_node40_t *node, uint32_t free_space) {
    aal_assert("vpf-022", node != NULL, return);
    
    set_nh40_free_space(reiserfs_node40_header(node), free_space);
}

static void reiserfs_node40_print(reiserfs_node40_t *node) {
    aal_assert("vpf-023", node != NULL, return);
}

/* 
    Returns -1 for item_pos if the wanted key goes before the first item of the node.
    Returns count for item_pos if after.
    Returns -1 for unit_pos if item_lookup method has not been implemented.
    Other values for unit_num are set by item lookup method.
*/
static reiserfs_coord_t *lookup(reiserfs_node40_t *node, reiserfs_key_t *key) {
    int ret;
    int64_t pos;
    reiserfs_plugin_id_t plugin_id;
    reiserfs_coord_t *coord;

    reiserfs_plugin_t *plugin;
    
    if (!node || !node->block || !key)
	return NULL;
	
    if (!(coord = aal_calloc(sizeof(*coord), 0)))
	return NULL;

    coord->node = node;
    
    if ((ret = reiserfs_misc_bin_search(key, &pos, reiserfs_node40_item_count(node), 
	node, reiserfs_node40_key_at, reiserfs_misc_comp_keys)) == -1)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Binary search failed on node %d.", 
	    aal_device_get_block_location(node->device, node->block));
	goto error_free_coord;
    }

    coord->item_pos = pos;
    coord->unit_pos = -1; 
    
    if (pos < 0 || pos >= reiserfs_node40_item_count (node)) {
	aal_free(coord);
	return coord;
    }
	
    if (!ret) {
	/* We need to search whithin the found item */
	plugin_id = get_ih40_plugin_id(reiserfs_node40_ih_at(node, pos));
	if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Node plugin cannot be find by its identifier %x.", plugin_id);
	    goto error_free_coord;
	}

/*	Uncomment this when method's interfaces will be ready
	if there is no item lookup method implemented, return coord with 
	unit_pos == -1.
	reiserfs_plugin_check_routine(plugin->item, lookup, return coord);
	plugin->item->common.lookup(key, coord);*/
    }
 
    aal_free(coord);
    return coord;

error_free_coord:
    aal_free(coord);
    return NULL;
}

static error_t reiserfs_node40_insert(reiserfs_coord_t *insert_into, 
    reiserfs_item_data_t *item_data) 
{
    uint32_t size, offset;
    reiserfs_node40_t * node;
    void *position, *end_position;
	
    aal_assert("vpf-006", insert_into != NULL, return -1);
    aal_assert("vpf-007", item_data != NULL, return -1);

    node = (reiserfs_node40_t *)insert_into->node;
    
    aal_assert("vpf-026", get_nh40_free_space(reiserfs_node40_header(node)) >= 
	item_data->length + sizeof(reiserfs_node40_header_t), return -1);
    aal_assert("vpf-027", insert_into->item_pos <= (int)reiserfs_node40_item_count(node), return -1);
    aal_assert("vpf-027", insert_into->unit_pos >= 0, return -1);

    /* First of all create an item if needed */

    /* Insert free space for item */

    position = reiserfs_node40_item_at(node, reiserfs_node40_item_count(node) - 1) + 	
	get_ih40_length(reiserfs_node40_ih_at(node, reiserfs_node40_item_count(node) - 1));
    
    size = insert_into->item_pos == (int)reiserfs_node40_item_count(node) ?
	0 : position - reiserfs_node40_item_at(node, insert_into->item_pos); 
    
    position -= size;

    aal_memcpy(position + item_data->length, position, size);

    /* Insert item */

    /* Insert item header */
    
    return 0;
}


static reiserfs_plugin_t node40_plugin = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "node40",
	    .desc = "Node for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},

	.open = (reiserfs_opaque_t *(*)(aal_device_t *, aal_block_t *))reiserfs_node40_open,
	.create = (reiserfs_opaque_t *(*)(aal_device_t *,aal_block_t *, uint8_t))reiserfs_node40_create,
	.confirm_format = (error_t (*)(reiserfs_opaque_t *))reiserfs_node40_confirm,
	.check =  (error_t (*)(reiserfs_opaque_t *, int))reiserfs_node40_check,
	.max_item_size = (uint32_t (*)(reiserfs_opaque_t *))reiserfs_node40_max_item_size,
	.max_item_num =  (uint32_t (*)(reiserfs_opaque_t *))reiserfs_node40_max_item_num,
	.item_count = (uint32_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_count,
	.get_free_space = (uint32_t (*)(reiserfs_opaque_t *))reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(reiserfs_opaque_t *, uint32_t))reiserfs_node40_set_free_space,
	.print = (void (*)(reiserfs_opaque_t *))reiserfs_node40_print
    }
};

reiserfs_plugin_t *reiserfs_node40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

reiserfs_plugin_register(reiserfs_node40_entry);

