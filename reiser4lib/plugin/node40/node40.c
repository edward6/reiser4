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

/* Here start_pos and end_pos must point to the same node */
static error_t reiserfs_node40_remove(reiserfs_coord_t *start_pos, 
    reiserfs_coord_t *end_pos) 
{
    aal_assert("vpf-010", start_pos != NULL, return -1);
    aal_assert("vpf-024", end_pos != NULL, return -1);
    aal_assert("vpf-025", reiserfs_node40_block(start_pos->node) != NULL, return -1);
    aal_assert("vpf-028", reiserfs_node40_block(end_pos) != NULL, return -1);
    
    return 0;
}

static error_t reiserfs_node40_move(reiserfs_node40_t *node_dest, reiserfs_node40_t *node_src,
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

    /* 
	Here will be more complex check of node validness 
	than in "confirm" routine.
    */
    
    return 0;
}

static uint16_t reiserfs_node40_item_max_size(reiserfs_node40_t *node) {
    aal_assert("vpf-016", node != NULL, return 0);

    return node->block->size - sizeof(reiserfs_node40_header_t) - 
	sizeof(reiserfs_item_header40_t);
}

static uint16_t reiserfs_node40_item_max_num(reiserfs_node40_t *node) {
    aal_assert("vpf-017", node != NULL, return 0);
   
    /*FIXME: this function probably should get item pligin id and call 
	item.common.minsize method
    */
    return (node->block->size - sizeof(reiserfs_node40_header_t)) / 
	(sizeof(reiserfs_item_header40_t) + 1);
}

static uint16_t reiserfs_node40_item_count(reiserfs_node40_t *node) {
    aal_assert("vpf-018", node != NULL, return 0);
    return get_nh40_num_items(reiserfs_node40_header(node));
}

/* this shound not be an interface method */
/*
static uint8_t reiserfs_node40_level(reiserfs_node40_t *node) {
    aal_assert("vpf-019", node != NULL, return 0);
    return get_nh40_level(reiserfs_node40_header(node));
}
*/
static uint16_t reiserfs_node40_get_free_space(reiserfs_node40_t *node) {
    aal_assert("vpf-020", node != NULL, return 0);
    return get_nh40_free_space(reiserfs_node40_header(node));
}

static void reiserfs_node40_set_free_space(reiserfs_node40_t *node, 
    uint32_t free_space) 
{
    aal_assert("vpf-022", node != NULL, return);
    set_nh40_free_space(reiserfs_node40_header(node), free_space);
}

static uint16_t reiserfs_node40_item_length(reiserfs_node40_t *node, int32_t pos) {
    aal_assert("vpf-037", node != NULL, return 0);
    return get_ih40_length(reiserfs_node40_ih_at(node, pos));    
}

static reiserfs_key_t *reiserfs_node40_item_min_key(reiserfs_node40_t *node, int32_t pos) {
    aal_assert("vpf-038", node != NULL, return NULL);
    return &reiserfs_node40_ih_at(node, pos)->key;    
}

static uint16_t reiserfs_node40_item_plugin_id(reiserfs_node40_t *node, int32_t pos) {
    aal_assert("vpf-039", node != NULL, return 0);
    return get_ih40_plugin_id(reiserfs_node40_ih_at(node, pos));    
}

static void *reiserfs_node40_item(reiserfs_node40_t *node, int32_t pos) {
    aal_assert("vpf-040", node != NULL, return NULL);
    return reiserfs_node40_item_at(node, pos);
}

/* Prepare text node description and push it into buff */
static void reiserfs_node40_print(reiserfs_node40_t *node, char *buff) {
    aal_assert("vpf-023", node != NULL, return);
    aal_assert("umka-457", buff != NULL, return);
}

/* 
    Returns -1 for item_pos if the wanted key goes before the first item of the node,
    count for item_pos if after and -1 for unit_pos if item_lookup method has not been 
    implemented. Other values for unit_num are set by item lookup method.
*/
static int reiserfs_node40_lookup(reiserfs_node40_t *node, reiserfs_key_t *key, 
    reiserfs_coord_t *coord) 
{
    int found; int64_t pos;
    reiserfs_plugin_id_t plugin_id;

    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-470", node != NULL, return 0);
    aal_assert("umka-472", key != NULL, return 0);
    aal_assert("umka-478", coord != NULL, return 0);
    aal_assert("umka-471", node->block != NULL, return 0);

    coord->node = node;
    
    if ((found = reiserfs_misc_bin_search(key, node, reiserfs_node40_item_count(node), 
	reiserfs_node40_key_at, reiserfs_misc_comp_keys, &pos)) == -1)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Search within the node %d failed.", 
	    aal_device_get_block_location(node->device, node->block));
	return 0;
    }

    coord->item_pos = pos;
    coord->unit_pos = -1;
    
    if (pos < 0 || pos >= reiserfs_node40_item_count(node))
	return found;

    if (!found) {
	/* We need to search whithin the found item */
	plugin_id = get_ih40_plugin_id(reiserfs_node40_ih_at(node, pos));
	if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_NODE_PLUGIN, plugin_id))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Node plugin cannot be find by its identifier %x.", plugin_id);
	    return found;
	}

/*	Uncomment this when item method's interfaces will be ready
	if there is no item lookup method implemented, return coord with 
	unit_pos == -1.
	
	reiserfs_check_method(plugin->item, lookup, return coord);
	plugin->item->common.lookup(key, coord);*/
    }
    
    return found;
}

static error_t reiserfs_node40_insert(reiserfs_coord_t *where, 
    reiserfs_item_info_t *item_info) 
{
    int num, i;
    uint32_t size, offset;
    reiserfs_node40_t * node;
    void *position, *end_position;
    reiserfs_item_header40_t *ih;
    reiserfs_plugin_id_t plugin_id;
    reiserfs_plugin_t *plugin;
	
    aal_assert("vpf-006", where != NULL, return -1);
    aal_assert("vpf-007", item_info != NULL, return -1);

    node = (reiserfs_node40_t *)where->node;
    
    aal_assert("vpf-026", get_nh40_free_space(reiserfs_node40_header(node)) >= 
	item_info->length + sizeof(reiserfs_node40_header_t), return -1);
    
    aal_assert("vpf-027", where->item_pos <= (int)reiserfs_node40_item_count(node), return -1);
    aal_assert("vpf-021", where->unit_pos >= 0, return -1);

    /* Insert free space for item */

    position = reiserfs_node40_item_at(node, reiserfs_node40_item_count(node) - 1) + 	
	get_ih40_length(reiserfs_node40_ih_at(node, reiserfs_node40_item_count(node) - 1));
    
    size = where->item_pos == (int)reiserfs_node40_item_count(node) ?
	0 : position - reiserfs_node40_item_at(node, where->item_pos); 
    
    position -= size;

    aal_memcpy(position + item_info->length, position, size);

    /* Insert item or create it if needed */

    plugin_id = item_info->plugin_id;
    
    if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Item plugin cannot be find by its identifier %x.", plugin_id);
    } else {
	/* Uncomment this when item methods will be implemented
	    item.create (position, data_info);
	*/
    }

    
    /* Insert item header */

    position = reiserfs_node40_ih_at(node, where->item_pos);

    size = (reiserfs_node40_item_count(node) - where->item_pos) 
	* sizeof (reiserfs_item_header40_t);
	
    aal_memcpy(position - size, position - size + sizeof (reiserfs_item_header40_t), size);

    /* update ih's->offset */
    /* Initialize item header */
    
    ih = (reiserfs_item_header40_t *)position;
    
    /* this should be probably changed to smth like 
       item.common.get_min_key or item_plugin.get_min_key */
    ih->key = *item_info->key;
    
    ih->plugin_id = item_info->plugin_id;
    ih->length = item_info->length;
    ih->offset = position - reiserfs_node40_data(where);
        
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
	.confirm = (error_t (*)(reiserfs_opaque_t *))reiserfs_node40_confirm,
	.check = (error_t (*)(reiserfs_opaque_t *, int))reiserfs_node40_check,
	.lookup = (int (*)(reiserfs_opaque_t *, reiserfs_key_t *, reiserfs_coord_t *))reiserfs_node40_lookup,
	
	.item_max_size = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_max_size,
	.item_max_num =  (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_max_num,
	.item_count = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_count,
	.item_length = (uint16_t (*)(reiserfs_opaque_t *, int32_t))reiserfs_node40_item_length,
	.item_min_key = (reiserfs_key_t *(*)(reiserfs_opaque_t *, int32_t))reiserfs_node40_item_min_key,
	.item_plugin_id = (uint16_t (*)(reiserfs_opaque_t *, int32_t))reiserfs_node40_item_plugin_id,
	.item = (void *(*)(reiserfs_opaque_t *, int32_t))reiserfs_node40_item,
	
	.get_free_space = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(reiserfs_opaque_t *, uint32_t))reiserfs_node40_set_free_space,
	.print = (void (*)(reiserfs_opaque_t *, char *))reiserfs_node40_print
    }
};

reiserfs_plugin_t *reiserfs_node40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

reiserfs_plugin_register(reiserfs_node40_entry);

