/*
    node40.c -- reiser4 default node plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "node40.h"

extern reiser4_plugin_t node40_plugin;

static reiser4_core_t *core = NULL;

#ifndef ENABLE_COMPACT

static reiser4_entity_t *node40_create(aal_block_t *block, 
    uint8_t level) 
{
    node40_t *node;
    
    aal_assert("umka-806", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    node->block = block;
    node->plugin = &node40_plugin;
    
    /* Plugin setup was moved here because we should support reiser3 */
    nh40_set_pid(nh40(node->block), NODE_REISER40_ID);

    nh40_set_free_space(nh40(node->block), 
	node->block->size - sizeof(node40_header_t));
    
    nh40_set_free_space_start(nh40(node->block), 
	sizeof(node40_header_t));
    
    nh40_set_level(nh40(node->block), level);
    nh40_set_magic(nh40(node->block), NODE40_MAGIC);
    nh40_set_num_items(nh40(node->block), 0);
    
    return (reiser4_entity_t *)node;
}

#endif

static uint16_t node40_pid(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-827", node != NULL, return 0);
    return nh40_get_pid(nh40(node->block));
} 

static reiser4_entity_t *node40_open(aal_block_t *block) {
    node40_t *node;
    
    aal_assert("umka-807", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->block = block;
    node->plugin = &node40_plugin;
    
    if (nh40_get_pid(nh40(node->block)) != NODE_REISER40_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Plugin id (%u) does not match current plugin id (%u).", 
	    nh40_get_pid(nh40(node->block)), NODE_REISER40_ID);
	goto error_free_node;
    }
    
    return (reiser4_entity_t *)node;
    
error_free_node:
    aal_free(node);
    return NULL;
}

static errno_t node40_close(reiser4_entity_t *entity) {
    aal_assert("umka-825", entity != NULL, return -1);
    
    aal_free(entity);
    return 0;
}

/*
    Confirms that passed node corresponds current plugin. This is something like 
    "probe" method.
*/
static int node40_confirm(aal_block_t *block) {
    aal_assert("vpf-014", block != NULL, return 0);
    return -(nh40_get_magic(nh40(block)) != NODE40_MAGIC);
}

/* Returns item number in given block. Used for any loops through all items */
static uint16_t node40_count(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-018", node != NULL, return 0);
    return nh40_get_num_items(nh40(node->block));
}

static errno_t node40_get_key(reiser4_entity_t *entity, 
    reiser4_pos_t *pos, reiser4_key_t *key) 
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-821", key != NULL, return -1);
    aal_assert("vpf-009", node != NULL, return -1);
    aal_assert("umka-939", pos != NULL, return -1);

    aal_assert("umka-810", pos->item < 
	nh40_get_num_items(nh40(node->block)), return -1);
        
    aal_memcpy(key->body, &(node40_ih_at(node->block, pos->item)->key), 
	sizeof(key40_t));
    
    return 0;
}

/* Gets item's body at given pos */
static void *node40_item_body(reiser4_entity_t *entity, 
    reiser4_pos_t *pos)
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-040", node != NULL, return NULL);
    aal_assert("umka-940", pos != NULL, return NULL);

    aal_assert("umka-814", pos->item < 
	nh40_get_num_items(nh40(node->block)), return NULL);
    
    return node40_ib_at(node->block, pos->item);
}

/*
    Retutns items overhead for this node format. Widely used in modification and 
    estimation routines.
*/
static uint16_t node40_overhead(reiser4_entity_t *entity) {
    return sizeof(item40_header_t);
}

/* Returns maximal size of item possible for passed node instance */
static uint16_t node40_maxspace(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-016", node != NULL, return 0);

    return node->block->size - sizeof(node40_header_t) - 
	sizeof(item40_header_t);
}

static uint16_t node40_item_pid(reiser4_entity_t *entity, 
    reiser4_pos_t *pos)
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-039", node != NULL, return INVALID_PLUGIN_ID);
    aal_assert("umka-941", pos != NULL, return INVALID_PLUGIN_ID);

    aal_assert("umka-815", pos->item < 
	nh40_get_num_items(nh40(node->block)), return 0);
    
    return ih40_get_pid(node40_ih_at(node->block, pos->item));
}

/* Returns length of item at pos */
static uint16_t node40_item_len(reiser4_entity_t *entity, 
    reiser4_pos_t *pos)
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-037", node != NULL, return 0);
    aal_assert("umka-942", pos != NULL, return 0);
    
    aal_assert("umka-815", pos->item < 
	nh40_get_num_items(nh40(node->block)), return 0);
    
    return ih40_get_len(node40_ih_at(node->block, pos->item));
}

#ifndef ENABLE_COMPACT

static errno_t node40_expand(node40_t *node, 
    reiser4_pos_t *pos, reiser4_item_hint_t *hint) 
{
    void *body;
    int i, item_pos;
    uint16_t offset;
    
    int is_insert;
    int is_space;
    int is_range;

    item40_header_t *ih;
    node40_header_t *nh;

    aal_assert("umka-817", node != NULL, return -1);
    aal_assert("vpf-006", pos != NULL, return -1);
    aal_assert("vpf-007", hint != NULL, return -1);

    is_space = (nh40_get_free_space(nh40(node->block)) >= 
	hint->len + (pos->unit == ~0ul ? sizeof(item40_header_t) : 0));
    
    is_range = (pos->item <= nh40_get_num_items(nh40(node->block)));
    
    aal_assert("vpf-026", is_space, return -1);
    aal_assert("vpf-027", is_range, return -1);

    is_insert = (pos->unit == ~0ul);
    item_pos = pos->item + !is_insert;
    
    nh = nh40(node->block);
    ih = node40_ih_at(node->block, item_pos);
    
    if (item_pos < nh40_get_num_items(nh)) {
        offset = ih40_get_offset(ih);

        aal_memmove(node->block->data + offset + hint->len, 
	    node->block->data + offset, nh40_get_free_space_start(nh) - offset);
	
	for (i = item_pos; i < nh40_get_num_items(nh); i++, ih--) 
	    ih40_set_offset(ih, ih40_get_offset(ih) + hint->len);

    	if (is_insert) {
	    aal_memmove(ih, ih + 1, sizeof(item40_header_t) * 
		(nh40_get_num_items(nh) - item_pos));
	}
	ih += (nh40_get_num_items(nh) - item_pos);
    } else
	offset = nh40_get_free_space_start(nh);
    
    nh40_set_free_space(nh, nh40_get_free_space(nh) - 
	hint->len - (is_insert ? sizeof(item40_header_t) : 0));
    
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) + 
	hint->len);
    
    if (!is_insert) {
	ih = node40_ih_at(node->block, pos->item);
	ih40_set_len(ih, ih40_get_len(ih) + hint->len);
	return 0;
    }
    
    aal_memcpy(&ih->key, hint->key.body, sizeof(ih->key));
    
    ih40_set_offset(ih, offset);
    ih40_set_pid(ih, hint->plugin->h.id);
    ih40_set_len(ih, hint->len);
    
    return 0;
}

/* Inserts item described by hint structure into node */
static errno_t node40_insert(reiser4_entity_t *entity, 
    reiser4_pos_t *pos, reiser4_item_hint_t *hint) 
{ 
    node40_header_t *nh;
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-818", node != NULL, return -1);
    aal_assert("vpf-119", pos != NULL, return -1);
    aal_assert("umka-908", pos->unit == ~0ul, return -1);
    
    if (!hint->data) {
	aal_assert("umka-712", hint->key.plugin != NULL, return -1);
    }
    
    if (node40_expand(node, pos, hint))
	return -1;

    nh = nh40(node->block);
    nh40_set_num_items(nh, nh40_get_num_items(nh) + 1);
    
    if (hint->data) {
	aal_memcpy(node40_ib_at(node->block, pos->item), hint->data, hint->len);
	return 0;
    }
    
    return plugin_call(return -1, hint->plugin->item_ops.common,
	init, node40_ib_at(node->block, pos->item), hint);
}

/* Pastes unit into item described by hint structure. */
static errno_t node40_paste(reiser4_entity_t *entity, 
    reiser4_pos_t *pos, reiser4_item_hint_t *hint) 
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-1017", node != NULL, return -1);
    aal_assert("vpf-120", pos != NULL && pos->unit != ~0ul, return -1);
    
    if (node40_expand(node, pos, hint))
	return -1;

    return plugin_call(return -1, hint->plugin->item_ops.common,
	insert, node40_ib_at(node->block, pos->item), pos->unit, hint);
}

static errno_t node40_shrink(node40_t *node,
    reiser4_pos_t *pos, uint16_t len) 
{
    int is_range;
    int is_move;
    int is_cut;
    
    uint16_t offset;
    node40_header_t *nh;
    item40_header_t *ih;
        
    aal_assert("umka-958", node != NULL, return -1);
    aal_assert("umka-959", pos != NULL, return -1);

    is_range = (pos->item < nh40_get_num_items(nh40(node->block)));
    aal_assert("umka-960", is_range, return -1);
    
    is_cut = (pos->unit != ~0ul);
    
    nh = nh40(node->block);
    ih = node40_ih_at(node->block, pos->item);
    offset = ih40_get_offset(ih);

    is_move = ((offset + ih40_get_len(ih)) < 
	nh40_get_free_space_start(nh));
    
    if (is_move) {
	item40_header_t *cur;
	item40_header_t *end;
	
	/* Moving the item bodies */
	aal_memmove(node->block->data + offset, node->block->data + 
	    offset + len, nh40_get_free_space_start(nh) - offset - len);
    
	/* Updating offsets */
	end = node40_ih_at(node->block, nh40_get_num_items(nh) - 1);

	for (cur = ih - 1; cur >= end; cur--)
	    ih40_set_offset(cur, ih40_get_offset(cur) - len);
	
	/* Moving headers */
	if (!is_cut)
	    aal_memmove(end + 1, end, ((void *)ih) - ((void *)end));
    }
	
    nh40_set_free_space_start(nh, nh40_get_free_space_start(nh) - len);
    
    return 0;
}

/* 
    This function removes item from the node at specified pos. Do not try to 
    understand it. This is impossible. But it works correctly.
*/
static errno_t node40_remove(reiser4_entity_t *entity, 
    reiser4_pos_t *pos) 
{
    uint16_t len;
    item40_header_t *ih;
    node40_header_t *nh;
    node40_t *node = (node40_t *)entity;

    aal_assert("umka-986", node != NULL, return -1);
    aal_assert("umka-987", pos != NULL, return -1);
    
    nh = nh40(node->block);
    ih = node40_ih_at(node->block, pos->item);
    len = ih40_get_len(ih);

    /* Removing either item or unit, depending on pos */
    if (node40_shrink(node, pos, len))
	return -1;
	
    nh40_set_num_items(nh, nh40_get_num_items(nh) - 1);
    nh40_set_free_space(nh, nh40_get_free_space(nh) + len +
        sizeof(item40_header_t));
	
    return 0;
}

static errno_t node40_cut(reiser4_entity_t *entity, 
    reiser4_pos_t *pos)
{
    void *body;
    uint16_t len;
    rpid_t pid;
    
    item40_header_t *ih;
    node40_header_t *nh;
    reiser4_plugin_t *plugin;
    node40_t *node = (node40_t *)entity;
	
    aal_assert("umka-988", node != NULL, return -1);
    aal_assert("umka-989", pos != NULL, return -1);
    
    nh = nh40(node->block);
    ih = node40_ih_at(node->block, pos->item);
    
    if ((pid = ih40_get_pid(ih)) == INVALID_PLUGIN_ID)
        return -1;
	
    if (!(plugin = core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin by its id 0x%x.", pid);
	return -1;
    }
    
    body = node40_ib_at(node->block, pos->item);
	
    if (!(len = plugin->item_ops.common.remove(body, pos->unit)))
        return -1;
	
    if (node40_shrink(node, pos, len))
        return -1;
	
    ih40_set_len(ih, ih40_get_len(ih) + len);
    nh40_set_free_space(nh, nh40_get_free_space(nh) + len);

    return 0;
}

#endif

static errno_t node40_valid(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-015", node != NULL, return -1);
    
    if (node40_confirm(node->block))
	return -1;

    return 0;
}

static uint16_t node40_space(reiser4_entity_t *entity) {
    node40_t *node = (node40_t *)entity;
    
    aal_assert("vpf-020", node != NULL, return 0);
    
    return nh40_get_free_space(nh40(node->block));
}

#ifndef ENABLE_COMPACT

static errno_t node40_set_key(reiser4_entity_t *entity, 
    reiser4_pos_t *pos, reiser4_key_t *key) 
{
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-819", key != NULL, return -1);
    aal_assert("umka-820", key->plugin != NULL, return -1);
    
    aal_assert("umka-809", node != NULL, return -1);
    aal_assert("umka-944", pos != NULL, return -1);
    
    aal_assert("umka-811", pos->item < 
	nh40_get_num_items(nh40(node->block)), return -1);

    aal_memcpy(&(node40_ih_at(node->block, pos->item)->key), key->body, 
	key->plugin->key_ops.size());

    return 0;
}

#endif

/* 
    Prepare text node description and push it into specied buffer. Caller should 
    decide what it should do with filled buffer.
*/
static errno_t node40_print(reiser4_entity_t *entity, 
    char *buff, uint32_t n, uint16_t options) 
{
    aal_assert("vpf-023", entity != NULL, return -1);
    aal_assert("umka-457", buff != NULL, return -1);

    return -1;
}

static inline void *callback_elem_for_lookup(void *node, 
    uint32_t pos, void *data)
{
    return &(node40_ih_at(((node40_t *)node)->block, pos)->key);
}

static inline int callback_comp_for_lookup(void *key1,
    void *key2, void *data)
{
    aal_assert("umka-566", key1 != NULL, return -1);
    aal_assert("umka-567", key2 != NULL, return -1);
    aal_assert("umka-656", data != NULL, return -1);

    return plugin_call(return -1, ((reiser4_plugin_t *)data)->key_ops, 
	compare, key1, key2);
}

static int node40_lookup(reiser4_entity_t *entity, 
    reiser4_key_t *key, reiser4_pos_t *pos)
{
    int lookup; int64_t item;
    node40_t *node = (node40_t *)entity;
    
    aal_assert("umka-472", key != NULL, return -1);
    aal_assert("umka-714", key->plugin != NULL, return -1);
    
    aal_assert("umka-478", pos != NULL, return -1);
    aal_assert("umka-470", node != NULL, return -1);

    if ((lookup = reiser4_comm_bin_search(node, nh40_get_num_items(nh40(node->block)), 
	    key->body, callback_elem_for_lookup, callback_comp_for_lookup, 
	    key->plugin, &item)) != -1)
	pos->item = item;

    return lookup;
}

static reiser4_plugin_t node40_plugin = {
    .node_ops = {
	.h = {
	    .handle = NULL,
	    .id = NODE_REISER40_ID,
	    .type = NODE_PLUGIN_TYPE,
	    .label = "node40",
	    .desc = "Node for reiserfs 4.0, ver. " VERSION,
	},
	.open		= node40_open,
	.close		= node40_close,
	
	.confirm	= node40_confirm,
	.valid		= node40_valid,
	
	.lookup		= node40_lookup,
	.print		= node40_print,
	.count		= node40_count,
	
	.overhead	= node40_overhead,
	.maxspace	= node40_maxspace,
	.space		= node40_space,
	.pid		= node40_pid,
	
	.get_key	= node40_get_key,
	
#ifndef ENABLE_COMPACT
	.create		= node40_create,
	.insert		= node40_insert,
	.remove		= node40_remove,
	.paste		= node40_paste,
	.cut		= node40_cut,
	.set_key	= node40_set_key,
#else
	.create		= NULL,
	.insert		= NULL,
	.remove		= NULL,
	.paste		= NULL,
	.cut		= NULL,
	
	.set_key	= NULL,
#endif
	.item_len	= node40_item_len,
	.item_body	= node40_item_body,
	.item_pid	= node40_item_pid,
    }
};

static reiser4_plugin_t *node40_start(reiser4_core_t *c) {
    core = c;
    return &node40_plugin;
}

plugin_register(node40_start);

