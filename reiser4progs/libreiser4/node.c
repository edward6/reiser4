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

reiserfs_node_t *reiserfs_node_create(aal_device_t *device, blk_t blk,
    reiserfs_plugin_id_t plugin_id, uint8_t level)
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
    
    set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, plugin_id);
    if (!(node->plugin = libreiser4_factory_find_by_coord(REISERFS_NODE_PLUGIN, plugin_id))) 
	libreiser4_factory_find_failed(REISERFS_NODE_PLUGIN, plugin_id, goto error_free_block);

    if (node->plugin->node.create != NULL ) {
        if (node->plugin->node.create(node->block, level)) {
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
    reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_node_t *node;

    aal_assert("umka-160", device != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    node->device = device;
    node->children = NULL;
    
    if (!(node->block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu.", blk);
	goto error_free_node;
    }
    
    if (plugin_id == REISERFS_GUESS_PLUGIN_ID) 
	plugin_id = reiserfs_node_get_plugin_id(node);
    
    if (!(node->plugin = libreiser4_factory_find_by_coord(REISERFS_NODE_PLUGIN, plugin_id))) 
	libreiser4_factory_find_failed(REISERFS_NODE_PLUGIN, plugin_id, goto error_free_block);

    if (node->plugin->node.open != NULL) {
        if (node->plugin->node.open (node->block)) {
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

error_t reiserfs_node_reopen(reiserfs_node_t *node, aal_device_t *device, 
    blk_t blk, reiserfs_plugin_id_t plugin_id) 
{
    aal_assert("umka-724", node != NULL, return -1);
    aal_assert("umka-725", device != NULL, return -1);
    
    return -1;
}

error_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-122", node != NULL, return -1);
    
    if (node->children) {
	aal_list_t *walk;
	
	aal_list_foreach_forward(walk, node->children)
	    reiserfs_node_close((reiserfs_node_t *)walk->item);

	aal_list_free(node->children);
	node->children = NULL;
    }

    if (node->plugin->node.close != NULL) {
        if (node->plugin->node.close (node->block)) {
            aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
                "Can't close node (%llu).", aal_block_get_nr(node->block));
            return -1;
        }
    }
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

error_t reiserfs_node_check(reiserfs_node_t *node, int flags) {
    aal_assert("umka-123", node != NULL, return -1);
    return libreiser4_plugin_call(return -1, node->plugin->node, 
	check, node->block, flags);
}

/*
    Makes search inside specified node by passed key. Fills
    passed coord by coords of found item and unit. This function
    is used by reiserfs_tree_lookup function.
*/
int reiserfs_node_lookup(reiserfs_node_t *node, reiserfs_key_t *key, 
    reiserfs_unit_coord_t *coord)
{
    int found; void *body;
    reiserfs_plugin_t *item_plugin;
    reiserfs_key_t max_key;
    
    aal_assert("umka-475", coord != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);
    aal_assert("umka-715", key->plugin != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);

    if ((found = libreiser4_plugin_call(return -1, node->plugin->node, 
	lookup, node->block, coord, key)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_get_nr(node->block));
	return -1;
    }

    if (found == 1)
	return 1;
   
    if (coord->item_pos == -1) 
	goto after_item;
	
    if (!(item_plugin = reiserfs_node_item_get_plugin(node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), coord->item_pos);
	return -1;
    }

    if (!(body = reiserfs_node_item_at(node, coord->item_pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), coord->item_pos);
	return -1;
    }
    
    /* 
	We are on the position where key is less then wanted. 
	Key could lies within the item or after the item.
    */
    reiserfs_key_init(&max_key, key->plugin);
    aal_memcpy(&max_key.body, reiserfs_node_item_key_at(node, coord->item_pos), 
	libreiser4_plugin_call(return -1, max_key.plugin->key, size,));
    
    if (item_plugin->item.common.max_key) {
	    
	if (item_plugin->item.common.max_key(&max_key) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		coord->item_pos, aal_block_get_nr(node->block));
	    return -1;
	}
	
	if (libreiser4_plugin_call(return -1, key->plugin->key, 
		compare, key->body, &max_key.body) > 0)
	    goto after_item;
    }

    if (!item_plugin->item.common.lookup)
	goto after_item;
	    
    if ((found = item_plugin->item.common.lookup(body, key, coord)) == -1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    coord->item_pos, aal_block_get_nr(node->block));
	return -1;
    }
    
    return found;

after_item:
    coord->item_pos++;
    coord->unit_pos = -1;
    return found;
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
	compare, reiserfs_node_item_key_at(node, 0), key->body);
}

/* Finds children node by its key in node cache */
reiserfs_node_t *reiserfs_node_find(reiserfs_node_t *node, 
    reiserfs_key_t *key)
{
    aal_list_t *list;
    
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
	reiserfs_node_item_key_at(node1, 0), reiserfs_node_item_key_at(node2, 0));
}

/* Connects children into sorted list of specified node */
error_t reiserfs_node_add(reiserfs_node_t *node, 
    reiserfs_node_t *children) 
{
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-561", node != NULL, return -1);
    aal_assert("umka-564", children != NULL, return -1);

    /* FIXME-UMKA: Hardcoded plugin id */
    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_KEY_PLUGIN, 0x0)))
	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, 0x0, return -1);
    
    node->children = aal_list_insert_sorted(node->children, 
	children, (int (*)(const void *, const void *, void *))
	callback_comp_for_insert, (void *)plugin);

    children->parent = node;
    
    return 0;   
}

/* Remove specified childern from node */
void reiserfs_node_remove(reiserfs_node_t *node, 
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
    children->parent = NULL;
}

#ifndef ENABLE_COMPACT

/*
    Synchronizes node's cache and frees all childrens.
    My be used when memory presure event will occur.
*/
error_t reiserfs_node_flush(reiserfs_node_t *node) {
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
	    "Can't synchronize block %llu to device.", 
	    aal_block_get_nr(node->block));
	return -1;
    }
    aal_list_free(node->children);
    node->children = NULL;
    
    aal_block_free(node->block);
    aal_free(node);
    return 0;
}

/* Just synchronuizes node's cache */
error_t reiserfs_node_sync(reiserfs_node_t *node) {
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
	    "Can't synchronize block %llu to device.", 
	    aal_block_get_nr(node->block));
	return -1;
    }
    return 0;
}

void reiserfs_node_set_level(reiserfs_node_t *node, uint8_t level) {
    aal_assert("umka-454", node != NULL, return);

    if (node->plugin->node.set_level == NULL)
        return;

    node->plugin->node.set_level(node->block, level);
}

void reiserfs_node_set_free_space(reiserfs_node_t *node, uint32_t value) {
    aal_assert("umka-456", node != NULL, return);
    
    return libreiser4_plugin_call(return, node->plugin->node, 
	set_free_space, node->block, value);
}

void reiserfs_node_set_plugin_id(reiserfs_node_t *node, reiserfs_plugin_id_t plugin_id) {
    aal_assert("umka-603", node != NULL, return);
    set_le16((reiserfs_node_header_t *)node->block->data, plugin_id, plugin_id);
}

#endif

uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->plugin->node.get_level == NULL)
        return 0;

    return node->plugin->node.get_level(node->block);
}

uint16_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node, 
	get_free_space, node->block);
}

reiserfs_plugin_id_t reiserfs_node_get_plugin_id(reiserfs_node_t *node) {
    aal_assert("umka-161", node != NULL, return -1);
    return get_le16((reiserfs_node_header_t *)node->block->data, plugin_id);
}

/* Item functions */
uint16_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node, 
	item_overhead, node->block);
}

uint16_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node, 
	item_maxsize, node->block);
}
    
uint16_t reiserfs_node_item_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node,
	item_maxnum, node->block);
}

uint16_t reiserfs_node_item_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node, 
	item_count, node->block);
}

void *reiserfs_node_item_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->plugin->node, 
	item_at, node->block, pos);
}

void *reiserfs_node_item_key_at(reiserfs_node_t *node, uint32_t pos) {
    aal_assert("umka-565", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->plugin->node, 
	item_key_at, node->block, pos);
}

reiserfs_plugin_id_t reiserfs_node_item_get_plugin_id(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("vpf-047", node != NULL, return 0);
    return libreiser4_plugin_call(return 0, node->plugin->node, 
	item_get_plugin_id, node->block, pos);
}

reiserfs_plugin_t *reiserfs_node_item_get_plugin(reiserfs_node_t *node, 
    uint32_t pos)
{
    aal_assert("umka-542", node != NULL, return 0);
    return libreiser4_factory_find_by_coord(REISERFS_ITEM_PLUGIN, 
	reiserfs_node_item_get_plugin_id(node, pos)); 
}

blk_t reiserfs_node_item_get_pointer(reiserfs_node_t *node, uint32_t pos) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }

    if (!(body = reiserfs_node_item_at(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    return libreiser4_plugin_call(return 0, plugin->item.specific.internal, 
	get_pointer, body);
}

int reiserfs_node_item_has_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return 0);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    
    if (!(body = reiserfs_node_item_at(node, pos))) {
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

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    return libreiser4_plugin_call(return 0, plugin->item.common, internal,);
}

#ifndef ENABLE_COMPACT

void reiserfs_node_item_set_pointer(reiserfs_node_t *node, 
    uint32_t pos, blk_t blk) 
{
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return);

    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return;
    }
    
    if (!(body = reiserfs_node_item_at(node, pos))) {
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
    
    1.Insertion of data: 
    a) coord->unit_pos == -1 
    b) hint->data != NULL
    c) get hint->plugin on the base of coord.
    2.Insertion of info: 
    a) coord->unit_pos == -1 
    b) hint->info != NULL
    c) hint->plugin != NULL
    3.Pasting of data: 
    a) coord->unit_pos != -1 
    b) hint->data != NULL
    c) get hint->plugin on the base of coord.
    4.Pasting of info: 
    a) coord->unit_pos != -1 
    b) hint->info != NULL
    c) get hint->plugin on the base of coord.
*/

error_t reiserfs_node_item_estimate(reiserfs_node_t *node, 
    reiserfs_item_hint_t *hint, reiserfs_unit_coord_t *coord)
{
    aal_assert("vpf-106", hint != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", coord != NULL, return -1);

    /* We must have item_info->plugin initialized for the 2nd case */
    aal_assert("vpf-118", coord->unit_pos != -1 || hint->info == NULL || 
	hint->plugin != NULL, return -1);
   
    if (!hint->plugin && !(hint->plugin = 
	reiserfs_node_item_get_plugin(node, coord->item_pos))) 
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
	hint, coord);
}

error_t reiserfs_node_item_insert(reiserfs_node_t *node, 
    reiserfs_unit_coord_t *coord, reiserfs_key_t *key, 
    reiserfs_item_hint_t *hint) 
{
    error_t ret;
    
    aal_assert("vpf-109", key != NULL, return -1);
    aal_assert("umka-720", key->plugin != NULL, return -1);
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", hint != NULL, return -1);
    aal_assert("vpf-108", coord != NULL, return -1);

    /* Estimate the size and check the free space */
    if (reiserfs_node_item_estimate(node, hint, coord)) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "Can't estimate space that item being inserted will consume.");
        return -1;
    }

    if (hint->length + reiserfs_node_item_overhead(node) >
        reiserfs_node_get_free_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the item of (%u) size in the node (%llu).",
            hint->length, aal_block_get_nr(node->block));
        return -1;
    }

    if (coord->unit_pos == -1) {
	if ((ret = libreiser4_plugin_call(return -1, node->plugin->node, item_insert, 
		node->block, coord, key, hint)) != 0)
	    return ret;
    } else {
	if ((ret = libreiser4_plugin_call(return -1, node->plugin->node, item_paste, 
		node->block, coord, key, hint)) != 0)
	    return ret;
    }
    
    /* Item must be inserted/unit pasted in item_insert/item_paste node methods. */
/*
    if (hint->plugin == NULL) {
        reiserfs_check_method(coord->node->plugin->node, item, return -1);

        aal_memcpy(coord->node->plugin->node.item(coord->node, coord->item_pos),
            hint->data, hint->length);
    } else {
        libreiser4_plugin_call(return -1, hint->plugin->item.common, create, 
	    coord, hint);
    }
*/
    return 0;
}

void reiserfs_node_item_set_plugin_id(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_id_t plugin_id)
{
    aal_assert("umka-551", node != NULL, return);
    libreiser4_plugin_call(return, node->plugin->node, item_set_plugin_id, 
	node->block, pos, plugin_id);
}

void reiserfs_node_item_set_plugin(reiserfs_node_t *node, 
    uint32_t pos, reiserfs_plugin_t *plugin)
{
    aal_assert("umka-552", node != NULL, return);
    aal_assert("umka-553", plugin != NULL, return);
    
    reiserfs_node_item_set_plugin_id(node, pos, plugin->h.id);
}

#endif

