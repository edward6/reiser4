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
    This is helper function. It is used for getting item's 
    key by given pos as callback function in reiserfs_misc_bin_search 
    function.
*/
static void *node40_item_key(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-009", block != NULL, return NULL);
    return &(node40_ih_at(block, pos)->key);
}

/* Gets item's body at given pos */
static void *node40_item_body(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-040", block != NULL, return NULL);
    return node40_ib_at(block, pos);
}

/*
    Retutns items overhead for this node format.
    Widely used in modification and estimation routines.
*/
static uint16_t node40_item_overhead(aal_block_t *block) {
    aal_assert("umka-543", block != NULL, return 0);
    return sizeof(reiserfs_ih40_t);
}

static uint16_t node40_item_maxsize(aal_block_t *block) {
    aal_assert("vpf-016", block != NULL, return 0);

    return block->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t);
}

/* Returns length of pos-th item */
static uint16_t node40_item_len(aal_block_t *block, uint32_t pos) {
    aal_assert("vpf-037", block != NULL, return 0);
    return ih40_get_length(node40_ih_at(block, pos));    
}

/* Gets/sets pos-th item's plugin identifier */
static uint16_t node40_item_get_pid(aal_block_t *block, 
    uint16_t pos) 
{
    aal_assert("vpf-039", block != NULL, return 0);
    return ih40_get_pid(node40_ih_at(block, pos));
}

/* Returns item number in given block. Used for any loops through all items */
static uint16_t node40_count(aal_block_t *block) {
    aal_assert("vpf-018", block != NULL, return 0);
    return nh40_get_num_items(reiserfs_nh40(block));
}

#ifndef ENABLE_COMPACT

static void node40_item_set_pid(aal_block_t *block, 
    uint16_t pos, uint16_t pid) 
{
    aal_assert("vpf-039", block != NULL, return);
    ih40_set_pid(node40_ih_at(block, pos), pid);
}

static errno_t node40_prepare(aal_block_t *block, reiserfs_pos_t *pos, 
    reiserfs_key_t *key, reiserfs_item_hint_t *item) 
{
    void *body;
    int i, item_pos;
    uint32_t offset;
    
    reiserfs_ih40_t *ih;
    reiserfs_nh40_t *nh;
    
    int is_enought_space;
    int is_inside_range;
    int is_new_item;

    aal_assert("vpf-006", pos != NULL, return -1);
    aal_assert("vpf-007", item != NULL, return -1);
    aal_assert("umka-712", key != NULL, return -1);
    aal_assert("umka-713", key->plugin != NULL, return -1);

    is_enought_space = (nh40_get_free_space(reiserfs_nh40(block)) >= 
	item->len + sizeof(reiserfs_ih40_t));

    is_inside_range = pos->item <= node40_count(block);
    
    aal_assert("vpf-026", is_enought_space, return -1);
    aal_assert("vpf-027", is_inside_range, return -1);

    is_new_item = (pos->unit == 0xffff);
    item_pos = pos->item + !is_new_item;
    
    nh = reiserfs_nh40(block);
    ih = node40_ih_at(block, item_pos);

    /* Insert free space for item and ih, change item heads */
    if (item_pos < nh40_get_num_items(nh)) {
	offset = ih40_get_offset(ih);

	aal_memcpy(block->data + offset + item->len, 
	    block->data + offset, nh40_get_free_space_start(nh) - offset);
	
	for (i = item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset(ih, ih40_get_offset(ih) + item->len);

	if (!is_new_item) {	    
	    ih = node40_ih_at(block, pos->item);
	    ih40_set_length(ih, ih40_get_length(ih) + item->len);
	} else {
	    /* 
		ih is set at the last item head - 1 in the last 
		for clause 
	    */
	    aal_memcpy(ih, ih + 1, sizeof(reiserfs_ih40_t) * 
		(node40_count(block) - item_pos)); 
	}
    } else {
	if (!is_new_item) 
	    return -1;
	
	offset = nh40_get_free_space_start(nh);
    } 
    
    /* Update node header */
    nh40_set_free_space(nh, nh40_get_free_space(nh) - 
	item->len - (is_new_item ? sizeof(reiserfs_ih40_t) : 0));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + 
	item->len);
    
    if (!is_new_item)	
	return 0;
    
    /* Create a new item header */
    aal_memcpy(&ih->key, key->body, libreiser4_plugin_call(return -1, 
	key->plugin->key, size,));
    
    ih40_set_offset(ih, offset);
    ih40_set_pid(ih, item->plugin->h.id);
    ih40_set_length(ih, item->len);
    
    return 0;
}

/* Inserts item described by hint structure into node. */
static errno_t node40_insert(aal_block_t *block, reiserfs_pos_t *pos, 
    reiserfs_key_t *key, reiserfs_item_hint_t *item) 
{ 
    reiserfs_nh40_t *nh;
    
    aal_assert("vpf-119", pos != NULL && 
	pos->unit == 0xffff, return -1);
    
    if (node40_prepare(block, pos, key, item))
	return -1;

    nh = reiserfs_nh40(block);
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
    
    if (item->data) {
	aal_memcpy(node40_ib_at(block, pos->item), item->data, 
	    item->len);

	return 0;
    } else {
	return libreiser4_plugin_call(return -1, item->plugin->item.common,
	    create, node40_ib_at(block, pos->item), item);
    }
}

/* 
    This function remove item pointed by pos from node. Do not try to understand
    this function. This is impossible. But it works correctly.
*/
static errno_t node40_remove(aal_block_t *block, reiserfs_pos_t *pos) {
    int do_move;
    uint32_t offset;
    reiserfs_nh40_t *nh;
    reiserfs_ih40_t *ih;
    
    reiserfs_ih40_t *ih_at_pos;
    reiserfs_ih40_t *ih_at_end;
    
    aal_assert("umka-762", block != NULL, return -1);
    aal_assert("umka-763", pos != NULL, return -1);

    nh = reiserfs_nh40(block);
    ih_at_pos = node40_ih_at(block, pos->item);

    aal_assert("umka-763", pos->item < 
	nh40_get_num_items(nh), return -1);
    
    /* Moving the data */
    offset = ih40_get_offset(ih_at_pos);

    do_move = ((offset + ih40_get_length(ih_at_pos)) < 
	nh40_get_free_space_start(nh));
    
    if (do_move) {
	aal_memcpy(block->data + offset, block->data + offset + 
	    ih40_get_length(ih_at_pos), nh40_get_free_space_start(nh) - 
	    offset - ih40_get_length(ih_at_pos));
    
	/* Updating offsets */
	ih_at_end = node40_ih_at(block, nh40_get_num_items(nh) - 1);
	for (ih = ih_at_pos + 1; ih >= ih_at_end; ih++)
	    ih40_set_offset(ih, ih40_get_offset(ih) - ih40_get_length(ih_at_pos));
    }
	
    /* 
	Updating node header. This is performed before moving the headers because
	we need ih_at_pos item length, that will be killed by moving.
    */
    nh40_set_free_space(nh, nh40_get_free_space(nh) + 
	ih40_get_length(ih_at_pos) + sizeof(reiserfs_ih40_t));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) - 
	ih40_get_length(ih_at_pos));
    
    if (do_move) {
	/* Moving the item headers */
	aal_memcpy(ih_at_end, ih_at_end + 1, 
	    ((void *)ih_at_pos) - ((void *)ih_at_end));
    }
    
    return 0;
}

/* Pastes units into item described by hint structure. */
static errno_t node40_paste(aal_block_t *block, reiserfs_pos_t *pos, 
    reiserfs_key_t *key, reiserfs_item_hint_t *item) 
{   
    /* 
	FIXME-UMKA: Vitaly, I think assertions for checking parameters should 
	be separated, because we need to know exactly, which one assertion has
	occured.
    */
    aal_assert("vpf-120", 
	pos != NULL && pos->unit != 0xffff, return -1);
    
    if (node40_prepare(block, pos, key, item))
	return -1;

    return libreiser4_plugin_call(return -1, item->plugin->item.common,
	insert, node40_ib_at(block, pos->item), pos->unit, item);
}

/*
    Creates empty node in passed block, previously filling 
    it zeros. This done because passed block may contains
    one old node.
*/
static errno_t node40_create(aal_block_t *block, uint8_t level) {
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

/* This function counts max item number */
static uint16_t node40_maxnum(aal_block_t *block) {
    uint16_t i;
    uint32_t total_size = 0;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-017", block != NULL, return 0);
   
    for (i = 0; i < nh40_get_num_items(reiserfs_nh40(block)); i++) {
	uint16_t pid = ih40_get_pid(node40_ih_at(block, i));
	
	if (!(plugin = factory->find(REISERFS_ITEM_PLUGIN, pid)))
	    libreiser4_factory_failed(return 0, find, item, pid);
	
	total_size += libreiser4_plugin_call(return 0, plugin->item.common, 
	    minsize,) + sizeof(reiserfs_ih40_t);
    }
    return (block->size - sizeof(reiserfs_nh40_t)) / total_size;
}

/*
    Confirms that passed corresponds current plugin.
    This is something like "probe" method.
*/
static errno_t node40_confirm(aal_block_t *block) {
    aal_assert("vpf-014", block != NULL, return -1);
    return -(nh40_get_magic(reiserfs_nh40(block)) != REISERFS_NODE40_MAGIC);
}

/*
    Makes more smart check for node validness. Will be
    used by fsck program.
*/
static errno_t node40_check(aal_block_t *block, int flags) {
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
    return (void *)node40_item_key(block, pos);
}

/*
    Callback function for comparing two keys. It is used
    by node40_lookup function.
*/
static int callback_compare_for_lookup(const void *key1,
    const void *key2, void *data)
{
    aal_assert("umka-566", key1 != NULL, return -1);
    aal_assert("umka-567", key2 != NULL, return -1);
    aal_assert("umka-656", data != NULL, return -1);

    return libreiser4_plugin_call(return -1, ((reiserfs_plugin_t *)data)->key, 
	compare, key1, key2);
}

static int node40_lookup(aal_block_t *block, reiserfs_pos_t *pos, 
    reiserfs_key_t *key) 
{
    int lookup; int64_t item;
    
    aal_assert("umka-472", key != NULL, return -1);
    aal_assert("umka-714", key->plugin != NULL, return -1);
    
    aal_assert("umka-478", pos != NULL, return -1);
    aal_assert("umka-470", block != NULL, return -1);
 
    if ((lookup = reiserfs_misc_bin_search((void *)block, node40_count(block), 
	    key->body, callback_elem_for_lookup, callback_compare_for_lookup, 
	    key->plugin, &item)) != -1)
	pos->item = item;

    return lookup;
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
	
	.confirm = (errno_t (*)(aal_block_t *))node40_confirm,
	.check = (errno_t (*)(aal_block_t *, int))node40_check,
	
	.lookup = (int (*)(aal_block_t *, void *, void *))
	    node40_lookup,
	
	.print = (void (*)(aal_block_t *, char *, uint16_t))
	    node40_print,
	
	.maxnum =  (uint16_t (*)(aal_block_t *))node40_maxnum,
	.count = (uint16_t (*)(aal_block_t *))node40_count,
	
	.get_level = (uint8_t (*)(aal_block_t *))
	    node40_get_level,
	
	.get_free_space = (uint16_t (*)(aal_block_t *))
	    node40_get_free_space,
	
#ifndef ENABLE_COMPACT
	.create = (errno_t (*)(aal_block_t *, uint8_t))node40_create,
	
	.insert = (errno_t (*)(aal_block_t *, void *, void *, void *))
	    node40_insert,
	
	.remove = (errno_t (*)(aal_block_t *, void *))node40_remove,
	
	.paste = (errno_t (*)(aal_block_t *, void *, void *, void *))
	    node40_paste,
	
	.set_level = (void (*)(aal_block_t *, uint8_t))
	    node40_set_level,
	
	.set_free_space = (void (*)(aal_block_t *, uint32_t))
	    node40_set_free_space,

	.item_set_pid = (void (*)(aal_block_t *, uint32_t, uint16_t))
	    node40_item_set_pid,
#else
	.create = NULL,
	.insert = NULL,
	.remove = NULL,
	.paste = NULL,
	.set_level = NULL,
	.set_free_space = NULL,
	.set_item_pid = NULL,
#endif
	.item_overhead = (uint16_t (*)(aal_block_t *))node40_item_overhead,
	.item_maxsize = (uint16_t (*)(aal_block_t *))node40_item_maxsize,
	
	.item_len = (uint16_t (*)(aal_block_t *, uint32_t))
	    node40_item_len,
	
	.item_body = (void *(*)(aal_block_t *, uint32_t))
	    node40_item_body,

	.item_key = (reiserfs_opaque_t *(*)(aal_block_t *, uint32_t))
	    node40_item_key,
	
	.item_get_pid = (uint16_t (*)(aal_block_t *, uint32_t))
	    node40_item_get_pid,
    }
};

static reiserfs_plugin_t *node40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &node40_plugin;
}

libreiser4_factory_register(node40_entry);

