/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include "node40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static void *reiserfs_node40_key_at(void *node, uint32_t pos) {
    aal_assert("vpf-009", node != NULL, return NULL);
    return &(reiserfs_node40_ih_at(node, pos)->key);
}

/* Here start_pos and end_pos must point to the same node */
static error_t reiserfs_node40_remove(reiserfs_coord_t *start_pos, 
    reiserfs_coord_t *end_pos) 
{
    aal_assert("vpf-010", start_pos != NULL, return -1);
    aal_assert("vpf-024", end_pos != NULL, return -1);
    aal_assert("vpf-025", reiserfs_node_block(start_pos->node) != NULL, return -1);
    aal_assert("vpf-028", reiserfs_node_block(end_pos) != NULL, return -1);
    
    return 0;
}

static error_t reiserfs_node40_move(reiserfs_node_t *node_dest, reiserfs_node_t *node_src,
    reiserfs_key_t *dest_key, reiserfs_key_t *src_key_from, reiserfs_key_t *src_key_to) 
{
    return 0;
}

/*
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
*/

static error_t reiserfs_node40_create(reiserfs_node_t *node, uint8_t level) 
{
    aal_assert("vpf-012", node != NULL, return -1);
    
    aal_memset(reiserfs_nh40(node), 0, sizeof(reiserfs_nh40_t));

    nh40_set_free_space(reiserfs_nh40(node), 
	reiserfs_node_block(node)->size - sizeof(reiserfs_nh40_t));
    
    nh40_set_free_space_start(reiserfs_nh40(node), sizeof(reiserfs_nh40_t));
    nh40_set_level(reiserfs_nh40(node), level);
    nh40_set_magic(reiserfs_nh40(node), REISERFS_NODE40_MAGIC);

    return 0;
}


static error_t reiserfs_node40_confirm(reiserfs_node_t *node) {
    aal_assert("vpf-014", node != NULL, return -1);
    
    return -(nh40_get_magic(reiserfs_nh40(node)) != REISERFS_NODE40_MAGIC);
}

static error_t reiserfs_node40_check(reiserfs_node_t *node, int flags) {
    aal_assert("vpf-015", node != NULL, return -1);
 
    if (nh40_get_magic(reiserfs_nh40(node)) != REISERFS_NODE40_MAGIC) 
	return -1;

    /* 
	Here will be more complex check for node validness 
	than in "confirm" routine.
    */
    
    return 0;
}

static uint16_t reiserfs_node40_item_overhead(reiserfs_node_t *node) {
    return sizeof(reiserfs_ih40_t);
}

static uint16_t reiserfs_node40_item_max_size(reiserfs_node_t *node) {
    aal_assert("vpf-016", node != NULL, return 0);

    return reiserfs_node_block(node)->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t);
}

static uint16_t reiserfs_node40_item_max_num(reiserfs_node_t *node) {
    aal_assert("vpf-017", node != NULL, return 0);
   
    /*FIXME: this function probably should get item pligin id and call 
	item.common.minsize method
    */
    return (node->block->size - sizeof(reiserfs_nh40_t)) / 
	(sizeof(reiserfs_ih40_t) + 1);
}

static uint16_t reiserfs_node40_item_count(reiserfs_node_t *node) {
    aal_assert("vpf-018", node != NULL, return 0);
    return nh40_get_num_items(reiserfs_nh40(node));
}

static uint8_t reiserfs_node40_get_level(reiserfs_node_t *node) {
    aal_assert("vpf-019", node != NULL, return 0);
    return nh40_get_level(reiserfs_nh40(node));
}

static void reiserfs_node40_set_level(reiserfs_node_t *node, uint8_t level) {
   aal_assert("vpf-043", node != NULL, return); 
   nh40_set_level(reiserfs_nh40(node), level);
}

static uint16_t reiserfs_node40_get_free_space(reiserfs_node_t *node) {
    aal_assert("vpf-020", node != NULL, return 0);
    return nh40_get_free_space(reiserfs_nh40(node));
}

static void reiserfs_node40_set_free_space(reiserfs_node_t *node, 
    uint32_t free_space) 
{
    aal_assert("vpf-022", node != NULL, return);
    nh40_set_free_space(reiserfs_nh40(node), free_space);
}

static uint16_t reiserfs_node40_item_length(reiserfs_node_t *node, uint16_t pos) {
    aal_assert("vpf-037", node != NULL, return 0);
    return ih40_get_length(reiserfs_node40_ih_at(node, pos));    
}

static uint16_t reiserfs_node40_item_plugin_id(reiserfs_node_t *node, uint16_t pos) {
    aal_assert("vpf-039", node != NULL, return 0);
    return ih40_get_plugin_id(reiserfs_node40_ih_at(node, pos));    
}

static void *reiserfs_node40_item(reiserfs_node_t *node, uint16_t pos) {
    aal_assert("vpf-040", node != NULL, return NULL);
    return reiserfs_node40_item_at(node, pos);
}

/* Prepare text node description and push it into buff */
static void reiserfs_node40_print(reiserfs_node_t *node, char *buff) {
    aal_assert("vpf-023", node != NULL, return);
    aal_assert("umka-457", buff != NULL, return);
}

/* 
    coord->item_pos = -1 if the wanted key goes before the first item of the node,
    count for item_pos if after and -1 for unit_pos if item_lookup method has not been 
    implemented. Other values for unit_num are set by item lookup method.
    Returns: 
    -1 if problem occured, 
    0 - exact match has not been found,
    1 - exact match has been found.
*/
static int reiserfs_node40_lookup(reiserfs_coord_t *coord, reiserfs_key_t *key) 
{
    int found; int64_t pos;
    reiserfs_plugin_id_t plugin_id;

    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-472", key != NULL, return 0);
    aal_assert("umka-478", coord != NULL, return 0);
    aal_assert("umka-470", coord->node != NULL, return 0);
 
    if ((found = reiserfs_misc_bin_search(key, coord->node, 
	reiserfs_node40_item_count(coord->node), reiserfs_node40_key_at, 
	reiserfs_misc_comp_keys, &pos)) == -1)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Search within the node %llu failed.", 
	    aal_device_get_block_nr(coord->node->device, coord->node->block));
	return -1;
    }

    coord->item_pos = pos;
    coord->unit_pos = -1;

    return found;
    
/* Hm, it seems we cannot build reiserfs_item_t here to call item lookup method.
   Move up and call lookup from api level.

    if (pos < 0 || pos >= reiserfs_node40_item_count(node))
	return found;
   
    if (!found) {
	// We need to search whithin the found item 
	plugin_id = ih40_get_plugin_id(reiserfs_node40_ih_at(node, pos));
	if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Item plugin cannot be find by its identifier %x.", plugin_id);
	    return found;
	}
	
	if (plugin->item.common.lookup != NULL) {
	    if (plugin->item.common.open) {

	    }
	    plugin->item->common.lookup(coord, key);
	}
    }
    
    return found;
*/  
}

static error_t reiserfs_node40_insert(reiserfs_coord_t *where, reiserfs_key_t *key,
    reiserfs_item_info_t *item_info) 
{
    int i;
    uint32_t offset;
//    void *position, *end_position;
    reiserfs_ih40_t *ih;
    reiserfs_nh40_t *nh;
	
    aal_assert("vpf-006", where != NULL, return -1);
    aal_assert("vpf-007", item_info != NULL, return -1);

    aal_assert("vpf-026", nh40_get_free_space(reiserfs_nh40(where->node)) >= 
	item_info->length + sizeof(reiserfs_nh40_t), return -1);
    
    aal_assert("vpf-027", where->item_pos <= reiserfs_node40_item_count(where->node), 
	return -1);
    aal_assert("vpf-061", where->item_pos >= 0, return -1);
    aal_assert("vpf-062", where->unit_pos == -1, return -1);

    nh = reiserfs_nh40(where->node);

    /* Insert free space for item and ih, change item heads */
    if (where->item_pos < nh40_get_num_items(nh)) {
	ih = reiserfs_node40_ih_at(where->node, where->item_pos);
	offset = ih40_get_offset(ih);
	
	aal_memcpy(reiserfs_node_data(where->node) + offset + item_info->length, 
	    reiserfs_node_data(where->node) + offset, 
	    nh40_get_free_space_start(nh) - offset);
	
	for (i = where->item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset (ih, ih40_get_offset(ih) + item_info->length);

	/* ih is set at the last item head - 1 in the last _for_ clause */
	aal_memcpy(ih, ih + 1, sizeof(reiserfs_ih40_t) * 
	    (reiserfs_node40_item_count(where->node) - where->item_pos));
    } else {
	offset = nh40_get_free_space_start(nh);
    }

    /* Create a new item header */
    ih = reiserfs_node40_ih_at(where->node, where->item_pos);
    aal_memcpy(&ih->key, key, sizeof(reiserfs_key_t));
    ih40_set_offset(ih, offset);
    ih40_set_plugin_id(ih, item_info->plugin->h.id);
    ih40_set_length(ih, item_info->length);
    
    /* Update node header */
    nh40_set_free_space(nh, nh40_get_free_space(nh) - item_info->length - 
	sizeof(reiserfs_ih40_t));
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + item_info->length);
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
    
    /* Insert item or create it if needed */
    if (item_info->plugin == NULL)
	aal_memcpy(reiserfs_node_data(where->node) + ih40_get_offset(ih), 
	    item_info->data, item_info->length);
    else {
	reiserfs_check_method (item_info->plugin->item.common, create, return -1);
	if (item_info->plugin->item.common.create (where, item_info)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Item plugin could not create an item (%d) in the node (%llu).", 
		where->item_pos,
		aal_device_get_block_nr(where->node->device, where->node->block));
	    return -1;
	}
    }

    return 0;
}

static reiserfs_plugin_t node40_plugin = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "node40",
	    .desc = "Node for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = NULL,
	.create = (error_t (*)(reiserfs_opaque_t *, uint8_t)) reiserfs_node40_create,
	.close = NULL,
	.confirm = (error_t (*)(reiserfs_opaque_t *))reiserfs_node40_confirm,
	.check = (error_t (*)(reiserfs_opaque_t *, int))reiserfs_node40_check,
	.lookup = (int (*)(reiserfs_opaque_t *, reiserfs_key_t *))
	    reiserfs_node40_lookup,
	.insert = (error_t (*)(reiserfs_opaque_t *, reiserfs_opaque_t *, 
	    reiserfs_opaque_t *))reiserfs_node40_insert,

	.item_overhead = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_overhead,
	.item_max_size = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_max_size,
	.item_max_num =  (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_max_num,
	.item_count = (uint16_t (*)(reiserfs_opaque_t *))reiserfs_node40_item_count,
	.item_length = (uint16_t (*)(reiserfs_opaque_t *, int32_t))
	    reiserfs_node40_item_length,
	.key_at = (reiserfs_key_t *(*)(reiserfs_opaque_t *, int32_t))
	    reiserfs_node40_key_at,
	.item_plugin_id = (uint16_t (*)(reiserfs_opaque_t *, int32_t))
	    reiserfs_node40_item_plugin_id,
	.item = (void *(*)(reiserfs_opaque_t *, int32_t))reiserfs_node40_item,
	
	.get_level = (uint8_t (*)(reiserfs_opaque_t *))reiserfs_node40_get_level,
	.set_level = (void (*)(reiserfs_opaque_t *, uint8_t))reiserfs_node40_set_level,
	.get_free_space = (uint16_t (*)(reiserfs_opaque_t *))
	    reiserfs_node40_get_free_space,
	.set_free_space = (void (*)(reiserfs_opaque_t *, uint32_t))
	    reiserfs_node40_set_free_space,
	.print = (void (*)(reiserfs_opaque_t *, char *))reiserfs_node40_print
    }
};

reiserfs_plugin_t *reiserfs_node40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

reiserfs_plugin_register(reiserfs_node40_entry);

