/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

errno_t reiserfs_node_rdkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_key_init(key, node->key_plugin);
    aal_memcpy(key->body, reiserfs_node_item_key(node, 
	reiserfs_node_count(node) - 1), sizeof(key->body));
    
    return 0;
}

errno_t reiserfs_node_ldkey(reiserfs_node_t *node, reiserfs_key_t *key) {
    
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_key_init(key, node->key_plugin);
    aal_memcpy(key->body, reiserfs_node_item_key(node, 0), 
	sizeof(key->body));

    return 0;
}

reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk,
    reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id, uint8_t level)
{
    reiserfs_node_t *node;
    
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->device = device;
    node->children = NULL;
    
    if (!(node->block = aal_block_alloc(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }
    
    aal_set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, node_plugin_id);

    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_plugin_id);

    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_plugin_id);
    
    if (node->node_plugin->node.create != NULL ) {
        if (node->node_plugin->node.create(node->block, level)) {
            aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Node plugin hasn't been able to create a node on block %llu.", blk);
            goto error_free_block;
        }
    }
     
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:    
    aal_free(node);

    return NULL;
}

#endif

reiserfs_node_t *reiserfs_node_open(aal_device_t *device, blk_t blk, 
    reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id) 
{
    reiserfs_node_t *node;

    aal_assert("umka-160", device != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    node->device = device;
    node->children = NULL;
    
    if (!(node->block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu. %s.", blk, aal_device_error(device));
	goto error_free_node;
    }
    
    if (node_plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	node_plugin_id = reiserfs_node_get_plugin_id(node);
    
    /* Finding the node plugin by its id */
    if (!(node->node_plugin = libreiser4_factory_find(REISERFS_NODE_PLUGIN, node_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, node, node_plugin_id);

    /* Finding the key plugin by its id */
    if (!(node->key_plugin = libreiser4_factory_find(REISERFS_KEY_PLUGIN, key_plugin_id))) 
	libreiser4_factory_failed(goto error_free_block, find, key, key_plugin_id);
    
    if (node->node_plugin->node.open != NULL) {
        if (node->node_plugin->node.open (node->block)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Node plugin hasn't been able to open a node %llu.", blk);
            goto error_free_block;
        }
    }
    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:
    aal_free(node);

    return NULL;
}

errno_t reiserfs_node_reopen(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk, reiserfs_id_t node_plugin_id, reiserfs_id_t key_plugin_id) 
{
    aal_assert("umka-724", node != NULL, return -1);
    aal_assert("umka-725", device != NULL, return -1);
    
    return -1;
}

errno_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return -1);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children)
	    reiserfs_node_close((reiserfs_node_t *)walk->item);

	aal_list_free(node->children);
	node->children = NULL;
    }

    if (node->node_plugin->node.close != NULL) {
        if (node->node_plugin->node.close (node->block)) {
            aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Can't close node (%llu).", aal_block_get_nr(node->block));
            return -1;
        }
    }
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

/* 
    Callback function for comparing node's left delimiting key
    with given key.
*/
static int callback_comp_for_find(reiserfs_node_t *node, 
    reiserfs_key_t *key, void *data)
{
    aal_assert("umka-653", node != NULL, return -2);
    aal_assert("umka-654", key != NULL, return -2);

    return libreiser4_plugin_call(return -2, key->plugin->key, 
	compare, reiserfs_node_item_key(node, 0), key->body);
}

/* Finds children node by its key in node cache */
reiserfs_node_t *reiserfs_node_find(reiserfs_node_t *node, 
    reiserfs_key_t *key)
{
    aal_list_t *list;
    
    if (!node->children)
	return NULL;
    
    if (!(list = aal_list_bin_search(node->children, key, 
	    (int (*)(const void *, const void *, void *))
	    callback_comp_for_find, NULL)))
	return NULL;

    return (reiserfs_node_t *)list->item;
}

/* 
    Callback function for comparing two nodes by their
    left delimiting keys.

    FIXME-UMKA: There should be using previously found
    key plugin to avoid overhead.
*/
static int callback_comp_for_insert(reiserfs_node_t *node1, 
    reiserfs_node_t *node2, reiserfs_plugin_t *plugin) 
{
    aal_assert("umka-648", node1 != NULL, return -2);
    aal_assert("umka-649", node2 != NULL, return -2);
    aal_assert("umka-719", plugin != NULL, return -2);

    return libreiser4_plugin_call(return -2, plugin->key, compare, 
	reiserfs_node_item_key(node1, 0), reiserfs_node_item_key(node2, 0));
}

/* Connects children into sorted list of specified node */
errno_t reiserfs_node_register(reiserfs_node_t *node, 
    reiserfs_node_t *children) 
{
    reiserfs_key_t key;
    reiserfs_node_t *left, *right;
    
    aal_assert("umka-561", node != NULL, return -1);
    aal_assert("umka-564", children != NULL, return -1);
    
    reiserfs_key_init(&key, node->key_plugin);
    aal_memcpy(key.body, reiserfs_node_item_key(children, 0), 
	sizeof(key.body));
    
    if (reiserfs_node_find(node, &key))
	return 0;

    node->children = aal_list_insert_sorted(node->children, 
	children, (int (*)(const void *, const void *, void *))
	callback_comp_for_insert, (void *)node->key_plugin);

    children->parent = node;
    
    left = node->children->prev ? node->children->prev->item : NULL;
    right = node->children->next ? node->children->prev->item : NULL;
    
    /* Setting up neighboors */
    children->right = right;
    children->left = left;
    
    if (right)
	right->left = children;
    
    if (left)
	left->right = children;
    
    return 0;
}

/* Remove specified childern from node */
void reiserfs_node_unregister(reiserfs_node_t *node, 
    reiserfs_node_t *children)
{
    aal_assert("umka-562", node != NULL, return);
    aal_assert("umka-563", children != NULL, return);

    if (node->children) {
	if (aal_list_length(aal_list_first(node->children)) == 1) {
	    aal_list_remove(node->children, children);
	    node->children = NULL;
	} else
	    aal_list_remove(node->children, children);
    }

    if (children->left)
	children->left->right = children->right;
    
    if (children->right)
	children->right->left = children->left;
    
    children->left = NULL;
    children->right = NULL;
    children->parent = NULL;
}

errno_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    return libreiser4_plugin_call(return -1, node->node_plugin->node, 
	check, node->block, flags);
}

int reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key, 
    reiserfs_pos_t *pos)
{
    int lookup; void *body;
    reiserfs_plugin_t *item_plugin;
    reiserfs_key_t max_key;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-715", key->plugin != NULL, return -1);

    pos->item = 0;
    pos->unit = -1;

    if ((lookup = libreiser4_plugin_call(return -1, 
	node->node_plugin->node, lookup, node->block, pos, key)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_get_nr(node->block));
	return -1;
    }

    if (lookup == 1)
	return 1;
   
    if (!(item_plugin = reiserfs_node_get_item_plugin(node, 
	pos->item > 0 ? pos->item - 1 : 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. 
	Key could lies within the item or after the item.
    */
    reiserfs_key_init(&max_key, key->plugin);
    aal_memcpy(&max_key.body, reiserfs_node_item_key(node, pos->item), 
	libreiser4_plugin_call(return -1, max_key.plugin->key, size,));
    
    if (item_plugin->item.common.max_key) {
	    
	if (item_plugin->item.common.max_key(&max_key) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_get_nr(node->block));
	    return -1;
	}
	
	if (libreiser4_plugin_call(return -1, key->plugin->key, 
	    compare, key->body, &max_key.body) > 0)
	{
	    pos->item++;
	    return lookup;
	}
    }

    if (!item_plugin->item.common.lookup)
	return lookup;
	    
    if (!(body = reiserfs_node_item_body(node, pos->item))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }
    
    if ((lookup = item_plugin->item.common.lookup(body, key, pos)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    pos->item, aal_block_get_nr(node->block));
	return -1;
    }
    
    return lookup;
}

/* Removes specified by pos item from node */
errno_t reiserfs_node_remove(reiserfs_node_t *node, reiserfs_pos_t *pos) {
    aal_assert("umka-767", node != NULL, return -1);
    aal_assert("umka-768", pos != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->node_plugin->node, 
	remove, node->block, pos);
}

static errno_t reiserfs_node_shift_item(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src, reiserfs_plugin_t *key_plugin, int remove) 
{
    int res;
    reiserfs_id_t item_plugin_id;
    reiserfs_item_hint_t item;
    
    aal_memset(&item, 0, sizeof(item));
    
    item.data = reiserfs_node_item_body(src->node, src->pos.item);
    item.length = reiserfs_node_item_length(src->node, src->pos.item);
    
    /* Preparing the key of new item */
    item.key.plugin = key_plugin;
    aal_memcpy(item.key.body, reiserfs_node_item_key(src->node, src->pos.item), 
        sizeof(item.key.body));
	
    item_plugin_id = reiserfs_node_get_item_plugin_id(src->node, src->pos.item);
	
    if (!(item.plugin = libreiser4_factory_find(REISERFS_ITEM_PLUGIN, 
        item_plugin_id)))
    {
	libreiser4_factory_failed(return -1, find, 
	    item, item_plugin_id);
    }

    /* Insering item into new location */
    if ((res = reiserfs_node_insert(dst->node, &dst->pos,
	    (reiserfs_key_t *)&item.key, &item)))
	return res;
    
    /* Remove src item */
    if (remove)
	res = reiserfs_node_remove(src->node, &src->pos);
    
    return res;
}

static errno_t reiserfs_node_move_item(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src, reiserfs_plugin_t *key_plugin) 
{
    return reiserfs_node_shift_item(dst, src, key_plugin, 1);
}

static errno_t reiserfs_node_copy_item(reiserfs_coord_t *dst, 
    reiserfs_coord_t *src, reiserfs_plugin_t *key_plugin) 
{
    return reiserfs_node_shift_item(dst, src, key_plugin, 0);
}

errno_t reiserfs_node_shift(reiserfs_coord_t *old, 
    reiserfs_coord_t *new) 
{
    int count, point;
    reiserfs_pos_t pos;
    reiserfs_key_t key;
    reiserfs_node_t *left;
    reiserfs_node_t *right;
    int left_moved, right_moved;
    
    reiserfs_item_hint_t item;
    reiserfs_id_t item_plugin_id;
    
    reiserfs_coord_t src, dst;
	
    aal_assert("umka-759", old != NULL, return -1);
    aal_assert("umka-766", new != NULL, return -1);
    
    left = old->node->left;
    right = old->node->right;
  
    left_moved = 0;
    point = old->pos.item;

    reiserfs_node_ldkey(old->node, &key);
    if (old->node->parent) {
	int res;
	
	if ((res = reiserfs_node_lookup(old->node->parent, &key, &pos)) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Lookup for shifted node is failed.");
	    return -1;
	}

	if (res == 0) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find shifted node in parent node.");
	    return -1;
	}
    }
    
    /* Trying to move items into the left nighbor */
    while (left && reiserfs_node_count(old->node) > 0 && 
	reiserfs_node_get_free_space(left) >= 
	reiserfs_node_item_length(old->node, 0) + 
	reiserfs_node_item_overhead(old->node))
    {
        src.node = old->node;
        src.pos.item = 0;
        src.pos.unit = -1;
	
        dst.node = left;
        dst.pos.item = reiserfs_node_count(left);
        dst.pos.unit = -1;
	
        if (reiserfs_node_move_item(&dst, &src, 
	    old->node->key_plugin)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Left shifting failed. Can't copy item.");
	    return -1;
	}
	left_moved++;
    }
    
    right_moved = 0;
    point = point - left_moved;
    count = reiserfs_node_count(old->node);
    
    /* Trying to move items into the right nighbor */
    while (right && reiserfs_node_count(old->node) > 0 && 
	reiserfs_node_get_free_space(right) >= 
	reiserfs_node_item_length(old->node, 
	reiserfs_node_count(old->node) - 1) + 
	reiserfs_node_item_overhead(old->node))
    {
        src.node = old->node;
        src.pos.item = reiserfs_node_count(old->node) - 1;
        src.pos.unit = -1;
	
        dst.node = right;
        dst.pos.item = 0;
        dst.pos.unit = -1;
	
        if (reiserfs_node_move_item(&dst, &src, 
	    old->node->key_plugin)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Left shifting failed. Can't copy item.");
	    return -1;
	}
	right_moved++;
    }
    
    /* Here we should update parent's internal keys */
    if (old->node->parent) {
	void *ldkey;

	if (pos.item > 0) {
	    
	    /* Updating ldkey for left neighbor */
	    if (!(ldkey = reiserfs_node_item_key(old->node->parent, pos.item - 1)))
		return -1;
	
	    reiserfs_node_ldkey(left, &key);
	    aal_memcpy(ldkey, key.body, sizeof(key.body));
	}
	
	/* Updating ldkey for shifted node */
	if (!(ldkey = reiserfs_node_item_key(old->node->parent, pos.item)))
	    return -1;
	
	reiserfs_node_ldkey(old->node, &key);
	aal_memcpy(ldkey, key.body, sizeof(key.body));

	if (pos.item < reiserfs_node_count(old->node->parent) - 1) {
	    
	    /* Updating ldkey for left neighbor */
	    if (!(ldkey = reiserfs_node_item_key(old->node->parent, pos.item + 1)))
		return -1;
	
	    reiserfs_node_ldkey(right, &key);
	    aal_memcpy(ldkey, key.body, sizeof(key.body));
	}
    }
    
    /* 
	Okay, and now we should find out where is our insertion point. It might be
	moved into one of neighbors.
    */
    if (point < 0) {
	    
	/* Insertion point was moved into left neighbor */
	new->node = left;
	new->pos.item = reiserfs_node_count(left) + point; 
	new->pos.unit = -1;
    } else if (right_moved >= (count - point)) {
	    
	/* Insertion point was moved into right neightbor */
	new->node = right;
	new->pos.item = right_moved - (count - point);
	new->pos.unit = -1;
    } else {
	    
	/* Insertion point stay in old node */
	new->node = old->node;
	new->pos.item = point;
	new->pos.unit = -1;
    }
    return 0;
}

uint16_t reiserfs_node_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node,
	maxnum, node->block);
}

uint16_t reiserfs_node_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	count, node->block);
}

errno_t reiserfs_node_insert(reiserfs_node_t *node, 
    reiserfs_pos_t *pos, reiserfs_key_t *key, 
    reiserfs_item_hint_t *item) 
{
    errno_t ret;
    
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("umka-720", key->plugin != NULL, return -1);
    
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", item != NULL, return -1);
    aal_assert("vpf-108", pos != NULL, return -1);

    if (!item->data) {
	/* 
	    Estimate the size that will be spent for item. This should be done
	    if item->data not installed.
	*/
	if (reiserfs_node_item_estimate(node, item, pos)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't estimate space that item being inserted will consume.");
	    return -1;
	}
    } else {
	aal_assert("umka-761", item->length > 0 && 
	    item->length < node->block->size - sizeof(reiserfs_node_header_t), return -1);
    }
    
    if (item->length + reiserfs_node_item_overhead(node) >
        reiserfs_node_get_free_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the item of (%u) size in the node (%llu).",
            item->length, aal_block_get_nr(node->block));
        return -1;
    }

    if (pos->unit == -1) {
        if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node, 
		insert, node->block, pos, key, item)) != 0)
	    return ret;
    } else {
	if ((ret = libreiser4_plugin_call(return -1, node->node_plugin->node, 
		paste, node->block, pos, key, item)) != 0)
	    return ret;
    }
    
    return 0;
}

#ifndef ENABLE_COMPACT

/*
    Synchronizes node's cache and frees all childrens.
    My be used when memory presure event will occur.
*/
errno_t reiserfs_node_flush(reiserfs_node_t *node) {
    aal_assert("umka-575", node != NULL, return 0);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children) {
	    if (reiserfs_node_flush((reiserfs_node_t *)walk->item))
		return -1;
	}
    }
    
    if (aal_block_write(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %llu to device. %s.", 
	    aal_block_get_nr(node->block), 
	    aal_device_error(node->device));
	return -1;
    }
    aal_list_free(node->children);
    node->children = NULL;
    
    aal_block_free(node->block);
    aal_free(node);
    return 0;
}

/* Just synchronuizes node's cache */
errno_t reiserfs_node_sync(reiserfs_node_t *node) {
    aal_assert("umka-124", node != NULL, return 0);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children) {
	    if (reiserfs_node_sync((reiserfs_node_t *)walk->item))
		return -1;
	}
    }
    
    if (aal_block_write(node->device, node->block)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't synchronize block %llu to device. %s.", 
	    aal_block_get_nr(node->block), 
	    aal_device_error(node->device));
	return -1;
    }
    return 0;
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);

    if (node->node_plugin->node.set_level == NULL)
        return;

    node->node_plugin->node.set_level(node->block, level);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    return libreiser4_plugin_call(return, node->node_plugin->node, 
	set_free_space, node->block, value);
}

void reiserfs_node_set_plugin_id(reiserfs_node_t *node, reiserfs_id_t plugin_id) {
    aal_assert("umka-603", node != NULL, return);
    aal_set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, plugin_id);
}

#endif

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->node_plugin->node.get_level == NULL)
        return 0;

    return node->node_plugin->node.get_level(node->block);
}

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	get_free_space, node->block);
}

reiserfs_id_t reiserfs_node_get_plugin_id(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return aal_get_le16((reiserfs_node_header_t *)node->block->data, plugin_id);
}

uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_overhead, node->block);
}

uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_maxsize, node->block);
}

uint16_t reiserfs_node_item_length(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-760", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	item_length, node->block, pos);
}

void *reiserfs_node_item_body(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->node_plugin->node, 
	item_body, node->block, pos);
}

void *reiserfs_node_item_key(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-565", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->node_plugin->node, 
	item_key, node->block, pos);
}

reiserfs_id_t reiserfs_node_get_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    return libreiser4_plugin_call(return 0, node->node_plugin->node, 
	get_item_plugin_id, node->block, pos);
}

reiserfs_plugin_t *reiserfs_node_get_item_plugin(reiserfs_node_t *node, 
    uint32_t pos) 
{
    reiserfs_id_t plugin_id;

    aal_assert("umka-755", node != NULL, return NULL);
    
    plugin_id = reiserfs_node_get_item_plugin_id(node, pos);
    return libreiser4_factory_find(REISERFS_ITEM_PLUGIN, plugin_id);
}

blk_t reiserfs_node_get_item_pointer(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    return libreiser4_plugin_call(return 0, plugin->item.specific.internal, 
	get_pointer, body);
}

int reiserfs_node_has_item_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    return libreiser4_plugin_call(return 0, plugin->item.specific.internal, 
	has_pointer, body, blk);
}

int reiserfs_node_item_internal(reiserfs_node_t *node, uint32_t pos) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-042", node != NULL, return 0);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    return libreiser4_plugin_call(return 0, plugin->item.common, internal,);
}

#ifndef ENABLE_COMPACT

void reiserfs_node_set_item_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return);

    if (!(plugin = reiserfs_node_get_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return;
    }
    
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return;
    }
    
    libreiser4_plugin_call(return, plugin->item.specific.internal, 
	set_pointer, body, blk);
}

/*
    We can estimate size for insertion and for pasting of hint->data (to be memcpy) 
    or of item_info->info (data to be created on the base of).
    
    1. Insertion of data: 
    a) pos->unit == -1 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    2. Insertion of info: 
    a) pos->unit == -1 
    b) hint->info != NULL
    c) hint->plugin != NULL
    
    3. Pasting of data: 
    a) pos->unit != -1 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    4. Pasting of info: 
    a) pos->unit_pos != -1 
    b) hint->info != NULL
    c) get hint->plugin on the base of pos.
*/

errno_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_item_hint_t *hint, reiserfs_pos_t *pos)
{
    aal_assert("vpf-106", hint != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", pos != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", pos->unit != -1 || 
	hint->plugin != NULL, return -1);
   
    if (!hint->plugin && !(hint->plugin = 
	reiserfs_node_get_item_plugin(node, pos->item))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }

    /* hint->has been already set for the 3rd case */
    if (hint->data != NULL)
	return 0;
    
    /* Estimate for the 2nd and for the 4th cases */
    return libreiser4_plugin_call(return -1, hint->plugin->item.common, estimate, 
	hint, pos);
}

void reiserfs_node_set_item_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_id_t plugin_id)
{
    aal_assert("umka-551", node != NULL, return);
    libreiser4_plugin_call(return, node->node_plugin->node, set_item_plugin_id, 
	node->block, pos, plugin_id);
}

#endif

