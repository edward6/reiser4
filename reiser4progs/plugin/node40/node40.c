/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include "node40.h"

static reiserfs_core_t *core = NULL;

#ifndef ENABLE_COMPACT

static reiserfs_node40_t *node40_create(aal_block_t *block, 
    uint8_t level) 
{
    reiserfs_node40_t *node;
    
    aal_assert("umka-806", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;
    
    /* Plugin setup was moved here because we should support reiser3 */
    nh40_set_pid(reiserfs_nh40(node->block), NODE_REISER40_ID);

    nh40_set_free_space(reiserfs_nh40(node->block), 
	node->block->size - sizeof(reiserfs_nh40_t));
    
    nh40_set_free_space_start(reiserfs_nh40(node->block), 
	sizeof(reiserfs_nh40_t));
    
    nh40_set_level(reiserfs_nh40(node->block), level);
    nh40_set_magic(reiserfs_nh40(node->block), REISERFS_NODE40_MAGIC);
    nh40_set_num_items(reiserfs_nh40(node->block), 0);
    
    return node;
}

#endif

static uint32_t node40_get_pid(reiserfs_node40_t *node) {
    aal_assert("umka-827", node != NULL, return 0);
    return nh40_get_pid(reiserfs_nh40(node->block));
} 

static reiserfs_node40_t *node40_open(aal_block_t *block) {
    reiserfs_node40_t *node;
    
    aal_assert("umka-807", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->block = block;
    
    if (node40_get_pid(node) != NODE_REISER40_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Plugin id (%u) does not match current plugin id (%u).", 
	    node40_get_pid(node), NODE_REISER40_ID);
	goto error_free_node;
    }
    
    return node;
    
error_free_node:
    aal_free(node);
    return NULL;
}

static errno_t node40_close(reiserfs_node40_t *node) {
    aal_assert("umka-825", node != NULL, return -1);
    aal_free(node);
    return 0;
}

/*
    Confirms that passed node corresponds current plugin. This is something like 
    "probe" method.
*/
static int node40_confirm(reiserfs_node40_t *node) {
    aal_assert("vpf-014", node != NULL, return 0);
    return -(nh40_get_magic(reiserfs_nh40(node->block)) != REISERFS_NODE40_MAGIC);
}

/* Returns item number in given block. Used for any loops through all items */
static uint32_t node40_count(reiserfs_node40_t *node) {
    aal_assert("vpf-018", node != NULL, return 0);
    return nh40_get_num_items(reiserfs_nh40(node->block));
}

static errno_t node40_get_key(reiserfs_node40_t *node, uint32_t pos, 
    reiserfs_key_t *key) 
{
    aal_assert("umka-821", key != NULL, return -1);
    aal_assert("vpf-009", node != NULL, return -1);
    aal_assert("umka-810", pos < node40_count(node), return -1);
    
    aal_memcpy(key->body, &(node40_ih_at(node->block, pos)->key), 
	sizeof(reiserfs_key40_t));
    
    return 0;
}

/* Gets item's body at given pos */
static void *node40_item_body(reiserfs_node40_t *node, uint32_t pos) {
    aal_assert("vpf-040", node != NULL, return NULL);
    aal_assert("umka-814", pos < node40_count(node), return NULL);
    
    return node40_ib_at(node->block, pos);
}

/*
    Retutns items overhead for this node format. Widely used in modification and 
    estimation routines.
*/
static uint32_t node40_item_overhead(void) {
    return sizeof(reiserfs_ih40_t);
}

/* Returns maximal size of item possible for passed node instance */
static uint32_t node40_item_maxsize(reiserfs_node40_t *node) {
    aal_assert("vpf-016", node != NULL, return 0);

    return node->block->size - sizeof(reiserfs_nh40_t) - 
	sizeof(reiserfs_ih40_t);
}

static uint32_t node40_item_get_pid(reiserfs_node40_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-039", node != NULL, return 0);
    aal_assert("umka-815", pos < node40_count(node), return 0);
    
    return ih40_get_pid(node40_ih_at(node->block, pos));
}

/* Returns length of item at pos */
static uint32_t node40_item_len(reiserfs_node40_t *node, uint32_t pos) {
    aal_assert("vpf-037", node != NULL, return 0);
    aal_assert("umka-815", pos < node40_count(node), return 0);
    
    return ih40_get_length(node40_ih_at(node->block, pos));    
}

#ifndef ENABLE_COMPACT

static errno_t node40_item_set_pid(reiserfs_node40_t *node, 
    uint32_t pos, uint32_t pid) 
{
    aal_assert("vpf-039", node != NULL, return -1);
    aal_assert("umka-816", pos < node40_count(node), return -1);

    ih40_set_pid(node40_ih_at(node->block, pos), pid);

    return 0;
}

static errno_t node40_prepare(reiserfs_node40_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item) 
{
    void *body;
    int i, item_pos;
    uint32_t offset;
    
    reiserfs_ih40_t *ih;
    reiserfs_nh40_t *nh;
    
    int is_insert;
    int is_enought_space;
    int is_inside_range;

    aal_assert("umka-817", node != NULL, return -1);
    aal_assert("vpf-006", pos != NULL, return -1);
    aal_assert("vpf-007", item != NULL, return -1);

    aal_assert("umka-712", item->key.plugin != NULL, return -1);

    is_enought_space = (nh40_get_free_space(reiserfs_nh40(node->block)) >= 
	item->len + sizeof(reiserfs_ih40_t));

    is_inside_range = (pos->item <= node40_count(node));
    
    aal_assert("vpf-026", is_enought_space, return -1);
    aal_assert("vpf-027", is_inside_range, return -1);

    is_insert = (pos->unit == 0xffff);
    item_pos = pos->item + !is_insert;
    
    nh = reiserfs_nh40(node->block);
    ih = node40_ih_at(node->block, item_pos);
    
    if (item_pos < nh40_get_num_items(nh)) {
        offset = ih40_get_offset(ih);

        aal_memmove(node->block->data + offset + item->len, 
	    node->block->data + offset, nh40_get_free_space_start(nh) - offset);
	
	for (i = item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset(ih, ih40_get_offset(ih) + item->len);

    	if (is_insert) {
	    aal_memmove(ih, ih + 1, sizeof(reiserfs_ih40_t) * 
		(nh40_get_num_items(nh) - item_pos));
	}
	ih += (nh40_get_num_items(nh) - item_pos);
    } else
	offset = nh40_get_free_space_start(nh);
    
    nh40_set_free_space(nh, nh40_get_free_space(nh) - 
	item->len - (is_insert ? sizeof(reiserfs_ih40_t) : 0));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + 
	item->len);
    
    if (!is_insert) {
	ih = node40_ih_at(node->block, pos->item);
	ih40_set_length(ih, ih40_get_length(ih) + item->len);
	return 0;
    }
    
    aal_memcpy(&ih->key, item->key.body, sizeof(ih->key));
    
    ih40_set_offset(ih, offset);
    ih40_set_pid(ih, item->plugin->h.id);
    ih40_set_length(ih, item->len);
    
    return 0;
}

/* Inserts item described by hint structure into node */
static errno_t node40_insert(reiserfs_node40_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item) 
{ 
    reiserfs_nh40_t *nh;
    
    aal_assert("umka-818", node != NULL, return -1);
    aal_assert("vpf-119", pos != NULL, return -1);
    aal_assert("umka-908", pos->unit == 0xffff, return -1);
    
    if (node40_prepare(node, pos, item))
	return -1;

    nh = reiserfs_nh40(node->block);
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
    
    if (item->data) {
	aal_memcpy(node40_ib_at(node->block, pos->item), 
	    item->data, item->len);
	return 0;
    } else {
	return libreiser4_plugin_call(return -1, item->plugin->item_ops.common,
	    create, node40_ib_at(node->block, pos->item), item);
    }
}

/* 
    This function removes item from the node at specified pos. Do not try to 
    understand it. This is impossible. But it works correctly.
*/
static errno_t node40_remove(reiserfs_node40_t *node, reiserfs_pos_t *pos) {
    int do_move;
    uint32_t offset;
    reiserfs_nh40_t *nh;
    reiserfs_ih40_t *ih;
    
    reiserfs_ih40_t *ih_at_pos;
    reiserfs_ih40_t *ih_at_end = NULL;
    
    aal_assert("umka-762", node != NULL, return -1);
    aal_assert("umka-763", pos != NULL, return -1);

    nh = reiserfs_nh40(node->block);
    
    aal_assert("umka-763", pos->item < nh40_get_num_items(nh), return -1);
    
    ih_at_pos = node40_ih_at(node->block, pos->item);
    
    /* Moving the data */
    offset = ih40_get_offset(ih_at_pos);

    do_move = ((offset + ih40_get_length(ih_at_pos)) < 
	nh40_get_free_space_start(nh));
    
    if (do_move) {
	aal_memmove(node->block->data + offset, node->block->data + offset + 
	    ih40_get_length(ih_at_pos), nh40_get_free_space_start(nh) - 
	    offset - ih40_get_length(ih_at_pos));
    
	/* Updating offsets */
	ih_at_end = node40_ih_at(node->block, nh40_get_num_items(nh) - 1);
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
    
    nh40_set_num_items(nh, nh40_get_num_items(nh) - 1);
    
    /* Moving the item headers */
    if (do_move) {
	aal_memmove(ih_at_end, ih_at_end + 1, ((void *)ih_at_pos) - 
	    ((void *)ih_at_end));
    }
    
    return 0;
}

/* Pastes unit into item described by hint structure. */
static errno_t node40_paste(reiserfs_node40_t *node, 
    reiserfs_pos_t *pos, reiserfs_item_hint_t *item) 
{   
    aal_assert("vpf-120", pos != NULL && pos->unit != 0xffff, return -1);
    
    if (node40_prepare(node, pos, item))
	return -1;

    return libreiser4_plugin_call(return -1, item->plugin->item_ops.common,
	insert, node40_ib_at(node->block, pos->item), pos->unit, item);
}

#endif

/* This function counts max item number */
static uint32_t node40_maxnum(reiserfs_node40_t *node) {
    uint32_t i;
    uint32_t total_size = 0;
    reiserfs_plugin_t *plugin = NULL;
    
    aal_assert("vpf-017", node != NULL, return 0);
   
    for (i = 0; i < nh40_get_num_items(reiserfs_nh40(node->block)); i++) {
	uint32_t pid = ih40_get_pid(node40_ih_at(node->block, i));
	
	if (!plugin || plugin->h.id != pid) {
	    if (!(plugin = core->factory_ops.plugin_find(ITEM_PLUGIN_TYPE, pid)))
		libreiser4_factory_failed(return 0, find, item, pid);
	}
	
	total_size += libreiser4_plugin_call(return 0, plugin->item_ops.common, 
	    minsize,) + sizeof(reiserfs_ih40_t);
    }
    return (node->block->size - sizeof(reiserfs_nh40_t)) / total_size;
}

/* Makes more smart check for node validness. Will be used by fsck program */
static errno_t node40_check(reiserfs_node40_t *node, int flags) {
    aal_assert("vpf-015", node != NULL, return -1);
    
    if (node40_confirm(node))
	return -1;

    /* 
	Here will be more complex check for node validness than in "confirm" 
	routine.
    */
    
    return 0;
}

static uint8_t node40_get_level(reiserfs_node40_t *node) {
    aal_assert("vpf-019", node != NULL, return 0);
    return nh40_get_level(reiserfs_nh40(node->block));
}

static uint32_t node40_get_space(reiserfs_node40_t *node) {
    aal_assert("vpf-020", node != NULL, return 0);
    return nh40_get_free_space(reiserfs_nh40(node->block));
}

#ifndef ENABLE_COMPACT

static errno_t node40_set_pid(reiserfs_node40_t *node, uint32_t pid) {
    aal_assert("umka-826", node != NULL, return -1);
    
    nh40_set_pid(reiserfs_nh40(node->block), pid);
    return 0;
}

static errno_t node40_set_level(reiserfs_node40_t *node, uint8_t level) {
   aal_assert("vpf-043", node != NULL, return -1); 
   nh40_set_level(reiserfs_nh40(node->block), level);

   return 0;
}

static errno_t node40_set_key(reiserfs_node40_t *node, uint32_t pos, 
    reiserfs_key_t *key) 
{
    aal_assert("umka-819", key != NULL, return -1);
    aal_assert("umka-820", key->plugin != NULL, return -1);
    
    aal_assert("umka-809", node != NULL, return -1);
    aal_assert("umka-811", pos < node40_count(node), return -1);

    aal_memcpy(&(node40_ih_at(node->block, pos)->key), key, 
	key->plugin->key_ops.size());

    return 0;
}

static void node40_set_space(reiserfs_node40_t *node, 
    uint32_t value)
{
    aal_assert("vpf-022", node != NULL, return);
    nh40_set_free_space(reiserfs_nh40(node->block), value);
}

#endif

/* 
    Prepare text node description and push it into specied buffer. Caller should 
    decide what it should do with filled buffer.
*/
static void node40_print(reiserfs_node40_t *node, 
    char *buff, uint32_t n) 
{
    aal_assert("vpf-023", node != NULL, return);
    aal_assert("umka-457", buff != NULL, return);
}

static inline void *callback_elem_for_lookup(void *node, 
    uint32_t pos, void *data)
{
    return &(node40_ih_at(((reiserfs_node40_t *)node)->block, pos)->key);
}

static inline int callback_comp_for_lookup(const void *key1,
    const void *key2, void *data)
{
    aal_assert("umka-566", key1 != NULL, return -1);
    aal_assert("umka-567", key2 != NULL, return -1);
    aal_assert("umka-656", data != NULL, return -1);

    return libreiser4_plugin_call(return -1, ((reiserfs_plugin_t *)data)->key_ops, 
	compare_full, key1, key2);
}

static int node40_lookup(reiserfs_node40_t *node, 
    reiserfs_key_t *key, reiserfs_pos_t *pos)
{
    int lookup; int64_t item;
    
    aal_assert("umka-472", key != NULL, return -1);
    aal_assert("umka-714", key->plugin != NULL, return -1);
    
    aal_assert("umka-478", pos != NULL, return -1);
    aal_assert("umka-470", node != NULL, return -1);

    if (node40_count(node) == 0)
	return 0;
    
    if ((lookup = reiserfs_misc_bin_search(node, node40_count(node), 
	    key->body, callback_elem_for_lookup, callback_comp_for_lookup, 
	    key->plugin, &item)) != -1)
	pos->item = item;

    return lookup;
}

static reiserfs_plugin_t node40_plugin = {
    .node_ops = {
	.h = {
	    .handle = NULL,
	    .id = NODE_REISER40_ID,
	    .type = NODE_PLUGIN_TYPE,
	    .label = "node40",
	    .desc = "Node for reiserfs 4.0, ver. " VERSION,
	},
	.open = (reiserfs_entity_t *(*)(aal_block_t *))node40_open,
	.close = (errno_t (*)(reiserfs_entity_t *))node40_close,
	
	.confirm = (int (*)(reiserfs_entity_t *))node40_confirm,
	.check = (errno_t (*)(reiserfs_entity_t *, int))node40_check,
	
	.lookup = (int (*)(reiserfs_entity_t *, reiserfs_key_t *, 
	    reiserfs_pos_t *))node40_lookup,
	
	.print = (errno_t (*)(reiserfs_entity_t *, char *, uint32_t))
	    node40_print,
	
	.maxnum =  (uint32_t (*)(reiserfs_entity_t *))node40_maxnum,
	.count = (uint32_t (*)(reiserfs_entity_t *))node40_count,
	
	.get_pid = (uint32_t (*)(reiserfs_entity_t *))node40_get_pid,
	.get_level = (uint8_t (*)(reiserfs_entity_t *))node40_get_level,
	
	.get_key = (errno_t (*)(reiserfs_entity_t *, uint32_t, reiserfs_key_t *))
	    node40_get_key,
	
	.get_space = (uint32_t (*)(reiserfs_entity_t *))node40_get_space,
	
#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(aal_block_t *, uint8_t))node40_create,
	
	.insert = (errno_t (*)(reiserfs_entity_t *, reiserfs_pos_t *, 
	    reiserfs_item_hint_t *))node40_insert,
	
	.paste = (errno_t (*)(reiserfs_entity_t *, reiserfs_pos_t *, 
	    reiserfs_item_hint_t *))node40_paste,
	
	.remove = (errno_t (*)(reiserfs_entity_t *, reiserfs_pos_t *))
	    node40_remove,
	
	.set_level = (errno_t (*)(reiserfs_entity_t *, uint8_t))node40_set_level,
	
	.set_pid = (errno_t (*)(reiserfs_entity_t *, uint32_t))node40_get_pid,
	
	.set_key = (errno_t (*)(reiserfs_entity_t *, uint32_t, reiserfs_key_t *))
	    node40_set_key,
	
	.set_space = (errno_t (*)(reiserfs_entity_t *, uint32_t))
	    node40_set_space,

	.item_set_pid = (errno_t (*)(reiserfs_entity_t *, uint32_t, uint32_t))
	    node40_item_set_pid,
#else
	.create = NULL,
	.insert = NULL,
	.remove = NULL,
	.paste = NULL,
	.set_pid = NULL,
	.set_level = NULL,
	.set_key = NULL,
	.set_space = NULL,
	.item_set_pid = NULL,
#endif
	.item_overhead = (uint32_t (*)(void))node40_item_overhead,
	
	.item_maxsize = (uint32_t (*)(reiserfs_entity_t *))node40_item_maxsize,
	.item_len = (uint32_t (*)(reiserfs_entity_t *, uint32_t))node40_item_len,
	.item_body = (void *(*)(reiserfs_entity_t *, uint32_t))node40_item_body,

	.item_get_pid = (uint32_t (*)(reiserfs_entity_t *, uint32_t))
	    node40_item_get_pid,
    }
};

static reiserfs_plugin_t *node40_entry(reiserfs_core_t *c) {
    core = c;
    return &node40_plugin;
}

libreiser4_factory_register(node40_entry);

