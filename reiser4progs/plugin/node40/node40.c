/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <reiser4/reiser4.h>
#include "node40.h"

/*
    Vitaly, I think node plugin's functions should accept aal_block_t, 
    not reiserfs_node_t. Plugins know nothing about API structures.
    And reiserfs_node_t is API structure. It is enought to know only
    block where node lies.

    About updating parent issue. I think and it is likely you agreed
    node plugin should not update parent node, because it doesn't know
    about its parent. Second reason is that, updating of parent is job
    of tree-balancing code, that will be on API level in library. Third 
    reason is that plugins should be as simple as possible. And finally
    fourth reason. We should not let user affect general library algorithms 
    such as balancing a lot. Because it is very simple to develop plugin
    that will break these algorithms and we will have a bug that will be 
    difficult to fix.
*/

static reiserfs_plugins_factory_t *factory = NULL;

/* 
    This is helper function. It is used for getting item's key by
    given pos as callback function in reiserfs_misc_bin_search function.
*/
static void *reiserfs_node40_key_at(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-009", block != NULL, return NULL);
    return &(node40_ih_at(block, pos)->key);
}

/* Gets item's body at given pos */
static void *reiserfs_node40_item_at(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-040", block != NULL, return NULL);
    return node40_item_at(block, pos);
}

/*
    Removes items from given block staring at "start" and 
    finishing "end". Wiil be used for fsck purposes.
*/
static error_t reiserfs_node40_remove(aal_block_t *block, 
    reiserfs_item_coord_t *start, reiserfs_item_coord_t *end) 
{
    aal_assert("vpf-010", start != NULL, return -1);
    aal_assert("vpf-024", end != NULL, return -1);
    aal_assert("vpf-025", block != NULL, return -1);
    
    return -1;
}

/*
    Moves a number of items (specified by "start" and "end") from one block
    to another. It is possible destination and source blocks shall be equal.
*/
static error_t reiserfs_node40_move(aal_block_t *block_dest, aal_block_t *block_src,
    reiserfs_key_t *dest, reiserfs_key_t *start, reiserfs_key_t *end) 
{
    return -1;
}

/*
    Creates empty node in passed block, previously filling 
    it zeros. This done because passed block may contains
    one old node.
*/
static error_t reiserfs_node40_create(aal_block_t *block, uint8_t level) {
    aal_assert("vpf-012", block != NULL, return -1);
    
    aal_memset(block->data, 0, block->size);

    nh40_set_free_space(reiserfs_nh40(block), 
	block->size - sizeof(reiserfs_nh40_t));
    
    nh40_set_free_space_start(reiserfs_nh40(block), 
	sizeof(reiserfs_nh40_t));
    
    nh40_set_level(reiserfs_nh40(block), level);
    nh40_set_magic(reiserfs_nh40(block), REISERFS_NODE40_MAGIC);
    nh40_set_num_items(reiserfs_nh40(block), 0);

    return 0;
}

/*
    Confirms that passed corresponds current plugin.
    This is something like "probe" method.
*/
static error_t reiserfs_node40_confirm(aal_block_t *block) {
    aal_assert("vpf-014", block != NULL, return -1);
    return -(nh40_get_magic(reiserfs_nh40(block)) != REISERFS_NODE40_MAGIC);
}

/*
    Makes more smart check for node validness. Will be
    used by fsck program.
*/
static error_t reiserfs_node40_check(aal_block_t *block, int flags) {
    aal_assert("vpf-015", block != NULL, return -1);
    
    if (reiserfs_node40_confirm(block))
	return -1;

    /* 
	Here will be more complex check for node validness 
	than in "confirm" routine.
    */
    
    return 0;
}

/*
    Retutns items overhead for this node format.
    Widely used in modification and estimation routines.
*/
static uint16_t reiserfs_node40_item_overhead(aal_block_t *block) {
    aal_assert("umka-543", block != NULL, return 0);
    return sizeof(reiserfs_ih40_t);
}

/*
    Vitaly, what is the purpose of this method? It is gives max item's
    size? Then why it calculates it this maner? Probably we need to rename
    it something more suitable it purpose.
*/
static uint16_t reiserfs_node40_item_maxsize(aal_block_t *block) {
    aal_assert("vpf-016", block != NULL, return 0);
    return block->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t);
}

/* This function counts max item number */
static uint16_t reiserfs_node40_item_maxnum(aal_block_t *block) {
    uint16_t i;
    uint32_t total_size = 0;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-017", block != NULL, return 0);
   
    for (i = 0; i < nh40_get_num_items(reiserfs_nh40(block)); i++) {
	uint16_t plugin_id = ih40_get_plugin_id(node40_ih_at(block, i));
	if (!(plugin = factory->find_by_coords(REISERFS_ITEM_PLUGIN, plugin_id))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find item plugin by its id %x.", plugin_id);
	    return 0;
	}
	total_size += libreiserfs_plugins_call(return 0, plugin->item.common, 
	    minsize, node40_item_at(block, i)) + sizeof(reiserfs_ih40_t);
    }
    return (block->size - sizeof(reiserfs_nh40_t)) / total_size;
}

/*
    Returns item number in given block. Used for any loops
    through all items.
*/
static uint16_t reiserfs_node40_item_count(aal_block_t *block) {
    aal_assert("vpf-018", block != NULL, return 0);
    return nh40_get_num_items(reiserfs_nh40(block));
}

/* Gets/sets the level of given block. */
static uint8_t reiserfs_node40_get_level(aal_block_t *block) {
    aal_assert("vpf-019", block != NULL, return 0);
    return nh40_get_level(reiserfs_nh40(block));
}

static void reiserfs_node40_set_level(aal_block_t *block, uint8_t level) {
   aal_assert("vpf-043", block != NULL, return); 
   nh40_set_level(reiserfs_nh40(block), level);
}

/* Gets/sets free space in given block. */
static uint16_t reiserfs_node40_get_free_space(aal_block_t *block) {
    aal_assert("vpf-020", block != NULL, return 0);
    return nh40_get_free_space(reiserfs_nh40(block));
}

static void reiserfs_node40_set_free_space(aal_block_t *block, 
    uint32_t free_space)
{
    aal_assert("vpf-022", block != NULL, return);
    nh40_set_free_space(reiserfs_nh40(block), free_space);
}

/* Returns length of pos-th item */
static uint16_t reiserfs_node40_item_length(aal_block_t *block, uint16_t pos) {
    aal_assert("vpf-037", block != NULL, return 0);
    return ih40_get_length(node40_ih_at(block, pos));    
}

/* Gets/sets pos-th item's plugin identifier */
static uint16_t reiserfs_node40_get_item_plugin_id(aal_block_t *block, 
    uint16_t pos) 
{
    aal_assert("vpf-039", block != NULL, return 0);
    return ih40_get_plugin_id(node40_ih_at(block, pos));
}

static void reiserfs_node40_set_item_plugin_id(aal_block_t *block, 
    uint16_t pos, uint16_t plugin_id) 
{
    aal_assert("vpf-039", block != NULL, return);
    ih40_set_plugin_id(node40_ih_at(block, pos), plugin_id);
}

/* 
    Prepare text node description and push it into buff.
    Caller should decide what it should do with filled buff.
*/
static void reiserfs_node40_print(aal_block_t *block, 
    char *buff, uint16_t n) 
{
    aal_assert("vpf-023", block != NULL, return);
    aal_assert("umka-457", buff != NULL, return);
}

/*
    We need to decide is key is plugin-specific value, or not.
    But for awhile this function will be using for comparing
    keys in node40 plugin.
*/
static int reiserfs_node40_key_cmp(const void *key1, const void *key2) {
    int result;
    reiserfs_key_t *k1, *k2;

    aal_assert("umka-566", key1 != NULL, return -2);
    aal_assert("umka-567", key2 != NULL, return -2);

    k1 = (reiserfs_key_t *)key1;
    k2 = (reiserfs_key_t *)key2;

    if ((result = KEY_COMP_ELEMENT(k1, k2, 0)) == 0) {
	if ((result = KEY_COMP_ELEMENT(k1, k2, 1)) == 0)
	    result = KEY_COMP_ELEMENT(k1, k2, 2);
    }

    return result;
}

/*
    Makes lookup inside the node and returns result of lookuping.

    coord->item_pos = -1 if the wanted key goes before the first item of the node,
    count for item_pos if after and -1 for unit_pos if item_lookup method has not been 
    implemented. Other values for unit_num are set by item lookup method.
    
    Returns: 
    -1 if problem occured, 
    0 - exact match has not been found,
    1 - exact match has been found.
*/

static int reiserfs_node40_lookup(aal_block_t *block, reiserfs_item_coord_t *coord, 
    reiserfs_key_t *key) 
{
    int found; int64_t pos;
    
    aal_assert("umka-472", key != NULL, return 0);
    aal_assert("umka-478", coord != NULL, return 0);
    aal_assert("umka-470", block != NULL, return 0);
 
    if ((found = reiserfs_misc_bin_search(key, (void *)block, 
	    reiserfs_node40_item_count(block), 
	    (void *(*)(void *, uint32_t))reiserfs_node40_key_at, 
	    reiserfs_node40_key_cmp, &pos)) == -1)
	return -1;

    /*
	FIXME-UMKA: Probably we need to make lookup inside found
	item (direntry item) for uint_pos.
    */
    
    coord->item_pos = pos;
    coord->unit_pos = -1;

    return found;
}

/*
    Inserts item described by item_info structure into node. Returns
    result of operation.
*/
static error_t reiserfs_node40_insert(aal_block_t *block, 
    reiserfs_item_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_info_t *item_info) 
{
    int i;
    uint32_t offset;
    reiserfs_ih40_t *ih;
    reiserfs_nh40_t *nh;

    aal_assert("vpf-006", coord != NULL, return -1);
    aal_assert("vpf-007", item_info != NULL, return -1);

    aal_assert("vpf-026", nh40_get_free_space(reiserfs_nh40(block)) >= 
	item_info->length + sizeof(reiserfs_nh40_t), return -1);
   
    aal_assert("vpf-027", coord->item_pos <= reiserfs_node40_item_count(block), 
	return -1);
    
    aal_assert("vpf-061", coord->item_pos >= 0, return -1);
    aal_assert("vpf-062", coord->unit_pos == -1, return -1);

    nh = reiserfs_nh40(block);

    /* Insert free space for item and ih, change item heads */
    if (coord->item_pos < nh40_get_num_items(nh)) {
	ih = node40_ih_at(block, coord->item_pos);
	offset = ih40_get_offset(ih);
	
	aal_memcpy(block->data + offset + item_info->length, 
	    block->data + offset, nh40_get_free_space_start(nh) - offset);
	
	for (i = coord->item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset(ih, ih40_get_offset(ih) + item_info->length);

	/* ih is set at the last item head - 1 in the last _for_ clause */
	aal_memcpy(ih, ih + 1, sizeof(reiserfs_ih40_t) * 
	    (reiserfs_node40_item_count(block) - coord->item_pos));
    } else
	offset = nh40_get_free_space_start(nh);

    /* Create a new item header */
    ih = node40_ih_at(block, coord->item_pos);
    aal_memcpy(&ih->key, key, sizeof(reiserfs_key_t));
    
    ih40_set_offset(ih, offset);
    ih40_set_plugin_id(ih, item_info->plugin->h.id);
    ih40_set_length(ih, item_info->length);
    
    /* Update node header */
    nh40_set_free_space(nh, nh40_get_free_space(nh) - 
	item_info->length - sizeof(reiserfs_ih40_t));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + 
	item_info->length);
    
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
   
    return 0;
}

#define NODE40_ID 0x0

static reiserfs_plugin_t node40_plugin = {
    .node = {
	.h = {
	    .handle = NULL,
	    .id = NODE40_ID,
	    .type = REISERFS_NODE_PLUGIN,
	    .label = "node40",
	    .desc = "Node for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.create = (error_t (*)(aal_block_t *, uint8_t))reiserfs_node40_create,
	.confirm = (error_t (*)(aal_block_t *))reiserfs_node40_confirm,
	.check = (error_t (*)(aal_block_t *, int))reiserfs_node40_check,
	
	.lookup = (int (*)(aal_block_t *, reiserfs_item_coord_t *, void *))
	    reiserfs_node40_lookup,
	
	.insert = (error_t (*)(aal_block_t *, reiserfs_item_coord_t *, 
	    void *, void *))reiserfs_node40_insert,
	
	.item_overhead = (uint16_t (*)(aal_block_t *))reiserfs_node40_item_overhead,
	.item_maxsize = (uint16_t (*)(aal_block_t *))reiserfs_node40_item_maxsize,
	.item_maxnum =  (uint16_t (*)(aal_block_t *))reiserfs_node40_item_maxnum,
	.item_count = (uint16_t (*)(aal_block_t *))reiserfs_node40_item_count,
	
	.item_length = (uint16_t (*)(aal_block_t *, int32_t))
	    reiserfs_node40_item_length,
	
	.key_at = (reiserfs_opaque_t *(*)(aal_block_t *, int32_t))
	    reiserfs_node40_key_at,

	.key_cmp = reiserfs_node40_key_cmp,
	
	.item_at = (void *(*)(aal_block_t *, int32_t))
	    reiserfs_node40_item_at,
	
	.get_item_plugin_id = (uint16_t (*)(aal_block_t *, int32_t))
	    reiserfs_node40_get_item_plugin_id,
	
	.set_item_plugin_id = (void (*)(aal_block_t *, int32_t, uint16_t))
	    reiserfs_node40_set_item_plugin_id,
	
	.get_level = (uint8_t (*)(aal_block_t *))
	    reiserfs_node40_get_level,
	
	.set_level = (void (*)(aal_block_t *, uint8_t))
	    reiserfs_node40_set_level,
	
	.get_free_space = (uint16_t (*)(aal_block_t *))
	    reiserfs_node40_get_free_space,
	
	.set_free_space = (void (*)(aal_block_t *, uint32_t))
	    reiserfs_node40_set_free_space,
	
	.print = (void (*)(aal_block_t *, char *, uint16_t))
	    reiserfs_node40_print
    }
};

reiserfs_plugin_t *reiserfs_node40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

libreiserfs_plugins_register(reiserfs_node40_entry);

