/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "node40.h"

static error_t reiserfs_node40_insert(reiserfs_coord_t *insert_into, reiserfs_item_data_t *item_data) 
{
    if (!insert_into || !insert_into->node || !item_data) 
	return -1;
    
    return 0;
}

#define node40_data(node) node->block->data
#define node40_block(node) node->block
#define node40_size(node)  node->block->size
#define node40_ih_at(node, pos) \
    ((reiserfs_item_header40_t *)(node40_data(node) + node40_size(node)) - pos - 1)
#define node40_item_id_at(node, pos) \
    node40_ih_at(node, pos)->plugin_id
    
typedef void reiserfs_opaque_t; 

static reiserfs_opaque_t *node40_key_at(reiserfs_opaque_t *node, int64_t pos) {
    reiserfs_node40_t *node40 = node;

    return &(node40_ih_at(node40, pos)->key);
}

typedef reiserfs_opaque_t *(get_element_to_comp_t)(reiserfs_opaque_t *, int64_t);
typedef int (comp_function_t)(reiserfs_opaque_t *, reiserfs_opaque_t *);

/* 
    this implements binary search for 'find_it' among 'count' elements.
    return values: 
    1 - key on *ppos  found exact key on *ppos position; 
    0 - exect key has not been found. key on *ppos < then 
*/
int reiserfs_bin_search (
    void * find_it,                 /* element to be found */
    int64_t * ppos,                 /* return position */
    uint32_t count,                 /* count of elements to look through */
    reiserfs_opaque_t *entity,      /* whose elements we are looking through */
    get_element_to_comp_t get_elem, /* function to get element */
    comp_function_t comp_func)      /* function to compare elements */
{
    int64_t rbound, lbound, j;
    int ret = 0;

    if (count == 0) {
        *ppos = -1;
        return 0;
    }

    lbound = 0;
    rbound = count - 1;

    for (j = (rbound + lbound) / 2; lbound <= rbound; j = (rbound + lbound) / 2) {
        ret =  comp_func (get_elem(entity, j), find_it);
        if (ret < 0) { /* second is greater */
            lbound = j + 1;
            continue;

        } else if (ret > 0) { /* first is greater */
            if (j == 0)
                break;
            rbound = j - 1;
            continue;
        } else { /* equal */
            *ppos = j;
            return 1;
        }
    }

    /* lbound == j, set *ppos on the position which element less than 'find_it' 
       on the base of the last search  */
    *ppos = lbound - (ret >= 0);

    return 0;
}

static int comp_keys40 (reiserfs_opaque_t *key1, reiserfs_opaque_t *key2) {
    return -1;
}

static error_t reiserfs_node40_remove(aal_block_t *block, reiserfs_key_t *start_key, 
	reiserfs_key_t *end_key) 
{
    reiserfs_coord_t *start_position, *end_position;
    
    if (!block || !start_key || !end_key) 
	return -1;
  
    if (comp_keys40(start_key, end_key) > 0) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Keys do not make up an interval. Removing skipped.");	
	return -1;
    }

    return 0;
}

static error_t reiserfs_node40_move(aal_block_t *block_dest, aal_block_t *block_src,
	reiserfs_key_t *dest_key, reiserfs_key_t *src_key_from, reiserfs_key_t *src_key_to) 
{
    return 0;
}

static reiserfs_node40_t *reiserfs_node40_open(aal_device_t *device, aal_block_t *block) {
    reiserfs_node40_t * node;
    
    if (!block || !device)
	return NULL;
    
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
    
    if (!block || !device) 
	return NULL;

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;
    node->device = device;

    aal_memset(node40_header(node), 0, sizeof (reiserfs_node40_header_t));
    set_nh40_free_space(node40_header(node), block->size - sizeof(reiserfs_node40_header_t));
    set_nh40_free_space_start(node40_header(node), block->size);
    set_nh40_level(node40_header(node), level);
    set_nh40_magic(node40_header(node), reiserfs_node_magic);

    return node;
}

static error_t reiserfs_node40_confirm(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return -1;
    
    return -(get_nh40_magic(node40_header(node)) != reiserfs_node_magic);
}

static error_t reiserfs_node40_check(reiserfs_node40_t *node, int flags) {
    if (!node || !node->block || !node->device) 
	return -1;
 
    if (get_nh40_magic(node40_header(node)) != reiserfs_node_magic) 
	return -1;    
 
    return 0;
}

static uint32_t reiserfs_node40_max_item_size(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;
 
    return 0;
}

static uint32_t reiserfs_node40_max_item_num(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;
    
    return 0;
}

static uint32_t reiserfs_node40_count(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;

    return get_nh40_num_items (node40_header(node));
}

static uint8_t reiserfs_node40_level(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;

    return get_nh40_level (node40_header(node));
}

static uint32_t reiserfs_node40_free_space(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;

    return get_nh40_level (node40_header(node));
}

static uint32_t reiserfs_node40_get_free_space(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return 0;

    return get_nh40_free_space (node40_header(node));
}

static void reiserfs_node40_set_free_space(reiserfs_node40_t *node, uint32_t free_space) {
    if (!node || !node->block || !node->device) 
	return;
    
    set_nh40_free_space (node40_header(node), free_space);
}
static void reiserfs_node40_print(reiserfs_node40_t *node) {
    if (!node || !node->block || !node->device) 
	return;
}

static reiserfs_coord_t *lookup (reiserfs_node40_t *node, reiserfs_key_t *key) {
    int64_t pos;
    int ret;
    reiserfs_coord_t *coord;
    reiserfs_item_plugin_t *plugin;
    
    if (!node || !node->block || !key)
	return NULL;
	
    if (!(coord = aal_calloc(sizeof(*coord), 0)))
	return NULL;

    coord->node = node;
    
    ret = reiserfs_bin_search (key, &pos, reiserfs_node40_count(node), 
	node, node40_key_at, comp_keys40);

    coord->item_num = pos;
    coord->unit_num = 0;
    
    if (pos < 0 || pos >= reiserfs_node40_count (node))
	return coord;
	
    if (!ret) {
	/* we need to search whithin the found item */
	
	plugin_id = node40_id_at(node, pos);
	if (!(plugin = reiserfs_plugins_find(REISERFS_NODE_PLUGIN, plugin_id))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Node plugin cannot be find by its identifier %x.", plugin_id);
	    goto error_free_coord;
	}


    reiserfs_plugin_check_routine(node->plugin->node, open, return NULL);
	plugin->common.lookup (key, coord);
    }
    
    return coord;
    
error_free_coord:
    aal_free(coord);
    
    return NULL;
}

static reiserfs_plugin_t node40_plugin = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "Node40",
	    .desc = "Node for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},

	.open = (reiserfs_node_opaque_t *(*)(aal_device_t *, aal_block_t *))reiserfs_node40_open,
	.create = (reiserfs_node_opaque_t *(*)(aal_device_t *,aal_block_t *, uint8_t))reiserfs_node40_create,
	.confirm_format = (error_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_confirm,
	.check =  (error_t (*)(reiserfs_node_opaque_t *, int))reiserfs_node40_check,
	.max_item_size = (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_max_item_size,
	.max_item_num =  (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_max_item_num,
	.count = (uint32_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_count,
	.level = (uint8_t (*)(reiserfs_node_opaque_t *))reiserfs_node40_level,
	.get_free_space = (uint32_t(*)(reiserfs_node_opaque_t *))reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(reiserfs_node_opaque_t *, uint32_t))reiserfs_node40_set_free_space,
	.print = (void (*)(reiserfs_node_opaque_t *))reiserfs_node40_print
    }
};

reiserfs_plugin_register (node40_plugin);

