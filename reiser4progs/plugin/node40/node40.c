/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "node40.h"

static reiserfs_plugin_factory_t *factory = NULL;

/* 
    This is helper function. It is used for getting item's key by
    given pos as callback function in reiserfs_misc_bin_search function.
*/
static void *node40_item_key_at(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-009", block != NULL, return NULL);
    return &(node40_ih_at(block, pos)->key);
}

/* Gets item's body at given pos */
static void *node40_item_at(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-040", block != NULL, return NULL);
    return node40_item_at_pos(block, pos);
}

#ifndef ENABLE_COMPACT

/*
    Removes item from given block at passed pos. It would
    be nice to be able to remove set of items, but due to
    high maintainability and clear of sources we dissable
    this ability for awhile.
*/
static error_t node40_item_remove(aal_block_t *block, 
    uint32_t pos) 
{
    aal_assert("vpf-025", block != NULL, return -1);
    return -1;
}

#endif

/*
    Retutns items overhead for this node format.
    Widely used in modification and estimation routines.
*/
static uint16_t node40_item_overhead(aal_block_t *block) {
    aal_assert("umka-543", block != NULL, return 0);
    return sizeof(reiserfs_ih40_t);
}

/*
    Vitaly, what is the purpose of this method? It is gives 
    max item's size? Then why it calculates it this maner? 
    Probably we need to rename it something more suitable it 
    purpose.
*/
static uint16_t node40_item_maxsize(aal_block_t *block) {
    aal_assert("vpf-016", block != NULL, return 0);
    return block->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t);
}

/* This function counts max item number */
static uint16_t node40_item_maxnum(aal_block_t *block) {
    uint16_t i;
    uint32_t total_size = 0;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-017", block != NULL, return 0);
   
    for (i = 0; i < nh40_get_num_items(reiserfs_nh40(block)); i++) {
	uint16_t plugin_id = ih40_get_plugin_id(node40_ih_at(block, i));
	
	if (!(plugin = factory->find_by_coord(REISERFS_ITEM_PLUGIN, plugin_id)))
	    libreiser4_factory_find_failed(REISERFS_ITEM_PLUGIN, plugin_id, return 0);
	
	total_size += libreiser4_plugin_call(return 0, plugin->item.common, 
	    minsize,) + sizeof(reiserfs_ih40_t);
    }
    return (block->size - sizeof(reiserfs_nh40_t)) / total_size;
}

/*
    Returns item number in given block. Used for any loops
    through all items.
*/
static uint16_t node40_item_count(aal_block_t *block) {
    aal_assert("vpf-018", block != NULL, return 0);
    return nh40_get_num_items(reiserfs_nh40(block));
}

/* Returns length of pos-th item */
static uint16_t node40_item_length(aal_block_t *block, uint16_t pos) {
    aal_assert("vpf-037", block != NULL, return 0);
    return ih40_get_length(node40_ih_at(block, pos));    
}

/* Gets/sets pos-th item's plugin identifier */
static uint16_t node40_item_get_plugin_id(aal_block_t *block, 
    uint16_t pos) 
{
    aal_assert("vpf-039", block != NULL, return 0);
    return ih40_get_plugin_id(node40_ih_at(block, pos));
}

#ifndef ENABLE_COMPACT

static void node40_item_set_plugin_id(aal_block_t *block, 
    uint16_t pos, uint16_t plugin_id) 
{
    aal_assert("vpf-039", block != NULL, return);
    ih40_set_plugin_id(node40_ih_at(block, pos), plugin_id);
}

static error_t node40_prepare_space(aal_block_t *block, 
    reiserfs_unit_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_hint_t *hint) 
{
    void *body;
    int i, item_pos;
    uint32_t offset;
    
    reiserfs_ih40_t *ih;
    reiserfs_nh40_t *nh;
    int is_enought_space, is_inside_range;
    int is_new_item = 0;

    aal_assert("vpf-006", coord != NULL, return -1);
    aal_assert("vpf-007", hint != NULL, return -1);
    aal_assert("umka-712", key != NULL, return -1);
    aal_assert("umka-713", key->plugin != NULL, return -1);

    is_enought_space = (nh40_get_free_space(reiserfs_nh40(block)) >= 
	hint->length + sizeof(reiserfs_ih40_t));

    is_inside_range = coord->item_pos <= node40_item_count(block);
    
    aal_assert("vpf-026", is_enought_space, return -1);
    aal_assert("vpf-027", is_inside_range, return -1);

    if (coord->unit_pos == -1) {
	is_new_item = 1;
	item_pos = coord->item_pos;
    } else
	item_pos = coord->item_pos + 1;
    
    nh = reiserfs_nh40(block);
    ih = node40_ih_at(block, item_pos);

    /* Insert free space for item and ih, change item heads */
    if (item_pos < nh40_get_num_items(nh)) {
	offset = ih40_get_offset(ih);

	aal_memcpy(block->data + offset + hint->length, 
		block->data + offset, nh40_get_free_space_start(nh) - offset);
	
	for (i = item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset(ih, ih40_get_offset(ih) + hint->length);

	if (!is_new_item) {	    
	    ih = node40_ih_at(block, coord->item_pos);
	    ih40_set_length(ih, ih40_get_length(ih) + hint->length);
	} else {
	    /* ih is set at the last item head - 1 in the last _for_ clause */
	    aal_memcpy(ih, ih + 1, sizeof(reiserfs_ih40_t) * 
		(node40_item_count(block) - item_pos)); 
	}
    } else {
	if (!is_new_item) 
	    return -1;
	
	offset = nh40_get_free_space_start(nh);
    } 
    
    /* Update node header */
    nh40_set_free_space(nh, nh40_get_free_space(nh) - 
	hint->length - (is_new_item ? sizeof(reiserfs_ih40_t) : 0));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + 
	hint->length);
    
    if (!is_new_item)	
	return 0;
    
    /* Create a new item header */
    aal_memcpy(&ih->key, key->body, libreiser4_plugin_call(return -1, 
	key->plugin->key, size,));
    
    ih40_set_offset(ih, offset);
    ih40_set_plugin_id(ih, hint->plugin->h.id);
    ih40_set_length(ih, hint->length);
    
    return 0;
}

/* Inserts item described by hint structure into node. */
static error_t node40_item_insert(aal_block_t *block, 
    reiserfs_unit_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_hint_t *hint) 
{ 
    reiserfs_nh40_t *nh;
    
    aal_assert("vpf-119", coord != NULL && coord->unit_pos == -1, return -1);
    
    if (node40_prepare_space(block, coord, key, hint))
	return -1;

    nh = reiserfs_nh40(block);
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
    
    return libreiser4_plugin_call(return -1, hint->plugin->item.common,
	create, node40_item_at_pos(block, coord->item_pos), hint);
}

/* Pastes units into item described by hint structure. */
static error_t node40_item_paste(aal_block_t *block, 
    reiserfs_unit_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_hint_t *hint) 
{   
    aal_assert("vpf-120", coord != NULL && coord->unit_pos != -1, return -1);
    
    if (node40_prepare_space(block, coord, key, hint))
	return -1;

    return libreiser4_plugin_call(return -1, hint->plugin->item.common,
	unit_add, node40_item_at_pos(block, coord->item_pos), coord, hint);
}

/*
    Creates empty node in passed block, previously filling 
    it zeros. This done because passed block may contains
    one old node.
*/
static error_t node40_create(aal_block_t *block, uint8_t level) {
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

#endif

/*
    Confirms that passed corresponds current plugin.
    This is something like "probe" method.
*/
static error_t node40_confirm(aal_block_t *block) {
    aal_assert("vpf-014", block != NULL, return -1);
    return -(nh40_get_magic(reiserfs_nh40(block)) != REISERFS_NODE40_MAGIC);
}

/*
    Makes more smart check for node validness. Will be
    used by fsck program.
*/
static error_t node40_check(aal_block_t *block, int flags) {
    aal_assert("vpf-015", block != NULL, return -1);
    
    if (node40_confirm(block))
	return -1;

    /* 
	Here will be more complex check for node validness 
	than in "confirm" routine.
    */
    
    return 0;
}

/* Gets/sets the level of given block. */
static uint8_t node40_get_level(aal_block_t *block) {
    aal_assert("vpf-019", block != NULL, return 0);
    return nh40_get_level(reiserfs_nh40(block));
}

/* Gets/sets free space in given block. */
static uint16_t node40_get_free_space(aal_block_t *block) {
    aal_assert("vpf-020", block != NULL, return 0);
    return nh40_get_free_space(reiserfs_nh40(block));
}

#ifndef ENABLE_COMPACT

static void node40_set_level(aal_block_t *block, uint8_t level) {
   aal_assert("vpf-043", block != NULL, return); 
   nh40_set_level(reiserfs_nh40(block), level);
}

static void node40_set_free_space(aal_block_t *block, 
    uint32_t free_space)
{
    aal_assert("vpf-022", block != NULL, return);
    nh40_set_free_space(reiserfs_nh40(block), free_space);
}

#endif

/* 
    Prepare text node description and push it into buff.
    Caller should decide what it should do with filled buff.
*/
static void node40_print(aal_block_t *block, 
    char *buff, uint16_t n) 
{
    aal_assert("vpf-023", block != NULL, return);
    aal_assert("umka-457", buff != NULL, return);
}

static void *callback_elem_for_lookup(void *block, uint32_t pos, 
    void *data)
{
    aal_assert("umka-655", block != NULL, return NULL);
    return (void *)node40_item_key_at(block, pos);
}

/*
    Callback function for comparing two keys. It is used
    by node40_lookup function.
*/
static int callback_compare_for_lookup(const void *key1,
    const void *key2, void *data)
{
    aal_assert("umka-566", key1 != NULL, return -2);
    aal_assert("umka-567", key2 != NULL, return -2);
    aal_assert("umka-656", data != NULL, return -2);

    return libreiser4_plugin_call(return -2, ((reiserfs_plugin_t *)data)->key, 
	compare, key1, key2);
}

/*
    Makes lookup inside the node and returns result of lookuping.

    coord->item_pos = -1 if the wanted key goes before the first item 
    of the node, count for item_pos if after. unit_num is preset on 0.
    
    Returns: 
    -1 if problem occured, 1(0) - exact match has (not) been found.
    
    NOTE: coord results differ from api node_lookup method.
*/

static int node40_lookup(aal_block_t *block, reiserfs_unit_coord_t *coord, 
    reiserfs_key_t *key) 
{
    int found; int64_t pos;
    
    aal_assert("umka-472", key != NULL, return -2);
    aal_assert("umka-714", key->plugin != NULL, return -2);
    aal_assert("umka-478", coord != NULL, return -2);
    aal_assert("umka-470", block != NULL, return -2);
 
    if ((found = reiserfs_misc_bin_search((void *)block, 
	    node40_item_count(block), key->body, callback_elem_for_lookup, 
	    callback_compare_for_lookup, key->plugin, &pos)) == -1)
	return -1;

    coord->item_pos = pos;
    coord->unit_pos = 0;    

    return found;
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
	.close = NULL,
	.confirm = (error_t (*)(aal_block_t *))node40_confirm,
	.check = (error_t (*)(aal_block_t *, int))node40_check,
	
	.lookup = (int (*)(aal_block_t *, void *, void *))
	    node40_lookup,
	
	.print = (void (*)(aal_block_t *, char *, uint16_t))
	    node40_print,
	
	.get_level = (uint8_t (*)(aal_block_t *))
	    node40_get_level,
	
	.get_free_space = (uint16_t (*)(aal_block_t *))
	    node40_get_free_space,
	
#ifndef ENABLE_COMPACT
	.create = (error_t (*)(aal_block_t *, uint8_t))node40_create,
	.set_level = (void (*)(aal_block_t *, uint8_t))
	    node40_set_level,
	
	.set_free_space = (void (*)(aal_block_t *, uint32_t))
	    node40_set_free_space,

	.item_insert = (error_t (*)(aal_block_t *, void *, void *, void *))
	    node40_item_insert,
	
	.item_paste = (error_t (*)(aal_block_t *, void *, void *, void *))
	    node40_item_insert,
	
	.item_set_plugin_id = (void (*)(aal_block_t *, int32_t, uint16_t))
	    node40_item_set_plugin_id,
	
#else
	.create = NULL,
	.set_level = NULL,
	.set_free_space = NULL,
	.item_insert = NULL,
	.item_paste = NULL,
	.item_set_plugin_id = NULL,
#endif
	.item_overhead = (uint16_t (*)(aal_block_t *))node40_item_overhead,
	.item_maxsize = (uint16_t (*)(aal_block_t *))node40_item_maxsize,
	.item_maxnum =  (uint16_t (*)(aal_block_t *))node40_item_maxnum,
	.item_count = (uint16_t (*)(aal_block_t *))node40_item_count,
	
	.item_length = (uint16_t (*)(aal_block_t *, int32_t))
	    node40_item_length,
	
	.item_at = (void *(*)(aal_block_t *, int32_t))
	    node40_item_at,

	.item_get_plugin_id = (uint16_t (*)(aal_block_t *, int32_t))
	    node40_item_get_plugin_id,
	
	.item_key_at = (reiserfs_opaque_t *(*)(aal_block_t *, int32_t))
	    node40_item_key_at,
    }
};

static reiserfs_plugin_t *node40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

libreiser4_factory_register(node40_entry);

