/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

/* Creates node on specified device and block and with spcified key plugin */
reiserfs_node_t *reiserfs_node_create(
    aal_device_t *device,	/* device new node will be created on */
    blk_t blk,			/* block number node will be created in */
    reiserfs_id_t pid,		/* node plugin id to be used */
    uint8_t level		/* node level */
) {
    reiserfs_node_t *node;
    
    aal_assert("umka-121", device != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    /* Allocating block in specified block number */
    if (!(node->block = aal_block_alloc(device, blk, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block %llu.", blk);
	goto error_free_node;
    }
    
    /* Finding the node plugin by its id */
    if (!(node->plugin = libreiser4_factory_find_by_id(REISERFS_NODE_PLUGIN, pid))) 
	libreiser4_factory_failed(goto error_free_block, find, node, pid);

    /* Requesting the plugin for initialization of the entity */
    if (!(node->entity = libreiser4_plugin_call(goto error_free_block, 
	node->plugin->node_ops, create, node->block, level))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create node entity.");
	goto error_free_block;
    }
    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:    
    aal_free(node);

    return NULL;
}

/* Saves specified node to its device */
errno_t reiserfs_node_sync(
    reiserfs_node_t *node	/* node to be save */
) {
    aal_assert("umka-798", node != NULL, return -1);
    return aal_block_write(node->block);
}

/* Updates key in specified position */
errno_t reiserfs_node_set_key(
    reiserfs_node_t *node,	/* node to be updated */
    uint32_t pos,		/* pos key will be updated in */
    reiserfs_key_t *key		/* key to be used */
) {
    aal_assert("umka-804", node != NULL, return -1);
    aal_assert("umka-805", key != NULL, return -1);

    libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	set_key, node->entity, pos, key);
    
    return 0;
}

#endif

/* This function is trying to detect node plugin */
static reiserfs_plugin_t *reiserfs_node_guess(
    aal_block_t *block		/* block node lies in */
) {
    reiserfs_id_t pid;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-902", block != NULL, return NULL);
    
    pid = *((uint16_t *)block->data);
    
    /* Finding node plugin by its id from node header */
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_NODE_PLUGIN, pid))) {
	/* FIXME-UMKA: Here will be further guessing code */
    }

    return plugin;
}

/* Opens node on specified device and block number */
reiserfs_node_t *reiserfs_node_open(
    aal_device_t *device,	/* device node will be opened on */
    blk_t blk			/* block number node will be opened in */
) {
    reiserfs_node_t *node;

    aal_assert("umka-160", device != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    /* Reading pased block from the device */
    if (!(node->block = aal_block_read(device, blk))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't read block %llu. %s.", blk, aal_device_error(device));
	goto error_free_node;
    }
    
    /* Finding the node plugin by its id */
    if (!(node->plugin = reiserfs_node_guess(node->block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't guess node plugin for node %llu.", blk);
	goto error_free_block;
    }
    
    /* 
	Initializing node's entity by means of calling "open" method of node
       	plugin.
    */
    if (!(node->entity = libreiser4_plugin_call(goto error_free_block, 
	node->plugin->node_ops, open, node->block)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialize node entity.");
	goto error_free_block;
    }
	    
    return node;
    
error_free_block:
    aal_block_free(node->block);
error_free_node:
    aal_free(node);
    return NULL;
}

/* Closes specified node */
errno_t reiserfs_node_close(reiserfs_node_t *node) {
    aal_assert("umka-824", node != NULL, return -1);
    aal_assert("umka-903", node->entity != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops,
	close, node->entity);
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

/* Gets right delimiting key from the specified node */
errno_t reiserfs_node_rdkey(
    reiserfs_node_t *node,	/* node the rdkey will be obtained from */
    reiserfs_key_t *key		/* key pointer to store the found rdkey */
) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_node_get_key(node, reiserfs_node_count(node) - 1, key);
    return 0;
}

/* Gets left delemiting key from the specified node */
errno_t reiserfs_node_ldkey(
    reiserfs_node_t *node,	/* node the ldkey will be obtained from */
    reiserfs_key_t *key		/* key pointer found key will be stored in */
) {
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiserfs_node_get_key(node, 0, key);
    return 0;
}

#ifndef ENABLE_COMPACT

/* 
    Relocates item from position specified by src_pos into position specified by
    dst_pos params. This function is used for cpying and moving items by balancing 
    code.
*/
static errno_t reiserfs_node_relocate(
    reiserfs_node_t *dst_node,	/* destination node */
    reiserfs_pos_t *dst_pos,	/* destination pos in destination node */
    reiserfs_node_t *src_node,	/* source node */
    reiserfs_pos_t *src_pos,	/* source position in source node */
    int remove			/* whether moved ite mshould be removed in src node */
) {
    errno_t res;
    reiserfs_id_t pid;
    reiserfs_item_hint_t item;

    aal_assert("umka-799", src_node != NULL, return -1);
    aal_assert("umka-800", dst_node != NULL, return -1);

    item.data = reiserfs_node_item_body(src_node, src_pos->item);
    item.len = reiserfs_node_item_len(src_node, src_pos->item);
    
    /* Getting the key of item that is going to be copied */
    reiserfs_node_get_key(src_node, src_pos->item, (reiserfs_key_t *)&item.key);
    
    pid = reiserfs_node_item_get_pid(src_node, src_pos->item);
	
    if (!(item.plugin = libreiser4_factory_find_by_id(REISERFS_ITEM_PLUGIN, pid)))
	libreiser4_factory_failed(return -1, find, item, pid);

    /* Insering the item into new location */
    if ((res = reiserfs_node_insert(dst_node, dst_pos, &item)))
	return res;
    
    /* Remove src item if remove flag is turned on */
    if (remove)
	res = reiserfs_node_remove(src_node, src_pos);
    
    return res;
}

/* 
    Wrapper for reiserfs_node_relocate function. This function actually copies
    item specified by src* params to dst* location. Parametrs meaning the same 
    as in reiserfs_node_relocate.
*/
errno_t reiserfs_node_copy(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos) 
{
    return reiserfs_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 0);
}

/* 
    Wrapper for reiserfs_node_relocate function. This function actually moves
    item specified by src* params to dst* location. Parameters meaning the same 
    as in previous one case.
*/
errno_t reiserfs_node_move(reiserfs_node_t *dst_node, 
    reiserfs_pos_t *dst_pos, reiserfs_node_t *src_node, 
    reiserfs_pos_t *src_pos) 
{
    return reiserfs_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 1);
}

/* 
    Splits node by means of moving right half of node into specified "right" node.
    This function is used by balancing code for splitting the internal nodes in 
    the case target internal node doesn't has enought free space for new node pointer.
*/
errno_t reiserfs_node_split(
    reiserfs_node_t *node,	/* node to be splitted */
    reiserfs_node_t *right	/* node right half of splitted node will be stored */
) {
    uint32_t median;
    reiserfs_pos_t dst_pos, src_pos;
    
    aal_assert("umka-780", node != NULL, return -1);
    aal_assert("umka-781", right != NULL, return -1);

    median = reiserfs_node_count(node) / 2;
    while (reiserfs_node_count(node) > median) {
	src_pos.item = reiserfs_node_count(node) - 1;
	src_pos.unit = 0xffff;
	
	dst_pos.item = 0;
	dst_pos.unit = 0xffff;
	
	if (reiserfs_node_move(right, &dst_pos, node, &src_pos))
	    return -1;
    }
    
    return 0;
}

/* Checks node for validness */
errno_t reiserfs_node_check(
    reiserfs_node_t *node,	/* node to be checked */
    int flags			/* some flags (not used at the moment) */
) {
    aal_assert("umka-123", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	check, node->entity, flags);
}

#endif

int reiserfs_node_confirm(reiserfs_node_t *node) {
    aal_assert("umka-123", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	confirm, node->entity);
}

/* 
    This function makes lookup inside specified node in order to find item/unit 
    stored in it.
*/
int reiserfs_node_lookup(
    reiserfs_node_t *node,	/* node to be grepped */
    reiserfs_key_t *key,	/* key to be find */
    reiserfs_pos_t *pos		/* found pos will be stored here */
) {
    reiserfs_key_t maxkey;
    int lookup; void *body;
    reiserfs_plugin_t *item_plugin;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);

    pos->item = 0;
    pos->unit = 0xffff;

    if (reiserfs_node_count(node) == 0)
	return 0;
   
    /* Calling node plugin */
    if ((lookup = libreiser4_plugin_call(return -1, 
	node->plugin->node_ops, lookup, node->entity, key, pos)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_get_nr(node->block));
	return -1;
    }

    if (lookup == 1) return 1;

    if (!(item_plugin = reiserfs_node_item_get_plugin(node, pos->item))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. Key could lies 
	within the item or after the item.
    */
    reiserfs_node_get_key(node, pos->item, &maxkey);
    
    if (item_plugin->item_ops.common.maxkey) {
	    
	if (item_plugin->item_ops.common.maxkey(&maxkey) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_get_nr(node->block));
	    return -1;
	}
	
	if (reiserfs_key_compare_full(key, &maxkey) > 0) {
	    pos->item++;
	    return 0;
	}
    } else {
	/* 
	    FIXME-UMKA: This is some dirty hack due to statdata plugin doesn't 
	    contains maxkey method. It probably should be changes soon, or lookup
	    will be redisigned.
	*/
	pos->item++;
	return 0;
    }

    /* Calling lookup method of found item (most probably direntry item) */
    if (!item_plugin->item_ops.common.lookup)
	return 0;
	    
    if (!(body = reiserfs_node_item_body(node, pos->item))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }
    
    if ((lookup = item_plugin->item_ops.common.lookup(body, key, 
	&pos->unit)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    pos->item, aal_block_get_nr(node->block));
	return -1;
    }

    return lookup;
}

/* Retutrns max possible number of items in specified node */
uint32_t reiserfs_node_maxnum(reiserfs_node_t *node) {
    aal_assert("umka-452", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops,
	maxnum, node->entity);
}

/* Returns real item count in specified node */
uint32_t reiserfs_node_count(reiserfs_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	count, node->entity);
}

#ifndef ENABLE_COMPACT

/* Removes specified by pos item from node */
errno_t reiserfs_node_remove(
    reiserfs_node_t *node,	/* node item will be removed from */
    reiserfs_pos_t *pos		/* position item will be removed at */
) {
    aal_assert("umka-767", node != NULL, return -1);
    aal_assert("umka-768", pos != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	remove, node->entity, pos);
}

/* Inserts item described by item hint into specified node at specified pos */
errno_t reiserfs_node_insert(
    reiserfs_node_t *node,	/* node new item will be inserted in */
    reiserfs_pos_t *pos,	/* position new item will be inserted at */
    reiserfs_item_hint_t *item	/* item hint */
) {
    errno_t ret;
    
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", item != NULL, return -1);
    aal_assert("vpf-108", pos != NULL, return -1);

    if (!item->data) {
	/* 
	    Estimate the size that will be spent for item. This should be done
	    if item->data not installed.
	*/
	if (item->len == 0 && reiserfs_node_item_estimate(node, pos, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't estimate space that item being inserted will consume.");
	    return -1;
	}
    } else {
	aal_assert("umka-761", item->len > 0 && 
	    item->len < node->block->size, return -1);
    }
    
    /* Checking if item length is gretter then free space in node */
    if (item->len + reiserfs_node_item_overhead(node) >
        reiserfs_node_get_free_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the item of (%u) size in the node (%llu).",
            item->len, aal_block_get_nr(node->block));
        return -1;
    }

    /* 
	Inserting new item or passting unit into one existent item pointed by 
	pos->item.
    */
    if (pos->unit == 0xffff) {
        if ((ret = libreiser4_plugin_call(return -1, node->plugin->node_ops, 
		insert, node->entity, pos, item)) != 0)
	    return ret;
    } else {
	if ((ret = libreiser4_plugin_call(return -1, node->plugin->node_ops, 
		paste, node->entity, pos, item)) != 0)
	    return ret;
    }
    
    return 0;
}

/* Updates node plugin id */
errno_t reiserfs_node_set_pid(
    reiserfs_node_t *node,	/* node to be updated */
    uint32_t pid		/* node plugin id to be used */
) {
    aal_assert("umka-828", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops,
	set_pid, node->entity, pid);
}

/* Sets node's level */
errno_t reiserfs_node_set_level(
    reiserfs_node_t *node,	/* node new level will be set */
    uint8_t level		/* level will be used */
) {
    aal_assert("umka-454", node != NULL, return -1);

    if (node->plugin->node_ops.set_level == NULL)
        return 0;

    return node->plugin->node_ops.set_level(node->entity, level);
}

/* Sets node free space */
errno_t reiserfs_node_set_free_space(
    reiserfs_node_t *node,	/* node to be updated */
    uint32_t value		/* free space to be set */
) {
    aal_assert("umka-456", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	set_free_space, node->entity, value);
}

#endif

/* Returns node plugin id in use */
uint32_t reiserfs_node_get_pid(
    reiserfs_node_t *node	/* node pid to be obtained */
) {
    aal_assert("umka-828", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops,
	get_pid, node->entity);
}

/* Returns level of specified node */
uint8_t reiserfs_node_get_level(reiserfs_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->plugin->node_ops.get_level == NULL)
        return 0;

    return node->plugin->node_ops.get_level(node->entity);
}

/* Returns free space of specified node */
uint32_t reiserfs_node_get_free_space(reiserfs_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	get_free_space, node->entity);
}

/* Returns overhead of specified node */
uint32_t reiserfs_node_item_overhead(reiserfs_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_overhead,);
}

/* Returns item max size from in specified node */
uint32_t reiserfs_node_item_maxsize(reiserfs_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_maxsize, node->entity);
}

/* Returns length of specified item */
uint32_t reiserfs_node_item_len(
    reiserfs_node_t *node,	/* node length of specified item wuill be obtained from */
    uint32_t pos		/* item position */
) {
    aal_assert("umka-760", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_len, node->entity, pos);
}

/* Returns body pointer of speficied item */
void *reiserfs_node_item_body(
    reiserfs_node_t *node,	/* node item body will be obtained from */
    uint32_t pos		/* item pos body will be obtained at */
) {
    aal_assert("umka-554", node != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->plugin->node_ops, 
	item_body, node->entity, pos);
}

/* Returns key from specified node at sepcified pos */
errno_t reiserfs_node_get_key(
    reiserfs_node_t *node,	/* node key will be got from */
    uint32_t pos,		/* pos key will be got at */
    reiserfs_key_t *key		/* place found key will be stored in */
) {
    errno_t res;
    
    aal_assert("umka-565", node != NULL, return -1);
    aal_assert("umka-803", key != NULL, return -1);
    
    if ((res = libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	    get_key, node->entity, pos, key)))
	return res;

    return ((key->plugin = reiserfs_key_guess(&key->body)) != NULL);
}

/* Returns item plugin id */
reiserfs_id_t reiserfs_node_item_get_pid(
    reiserfs_node_t *node,	/* node to be inspected */
    uint32_t pos		/* item pos plugin id will be obtained from */
) {
    aal_assert("vpf-047", node != NULL, return 0);
    aal_assert("umka-904", pos < reiserfs_node_count(node), return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_get_pid, node->entity, pos);
}

/* Returns item plugin by item pos */
reiserfs_plugin_t *reiserfs_node_item_get_plugin(
    reiserfs_node_t *node,	/* node plugin will be got from */
    uint32_t pos		/* item pos plugin will be obtained at */
) {
    aal_assert("umka-755", node != NULL, return NULL);
    
    return libreiser4_factory_find_by_id(REISERFS_ITEM_PLUGIN, 
	reiserfs_node_item_get_pid(node, pos));
}

/* Returns node pointer from internal node */
blk_t reiserfs_node_get_pointer(
    reiserfs_node_t *node,	/* node pointer will be obtained from */
    uint32_t pos		/* item position to be used */
) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);
    aal_assert("umka-778", pos < reiserfs_node_count(node), return 0);

    /* Checking if specified item isn't an internal item */
    if (!reiserfs_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get the node pointer from non-internal item.");
	return 0;
    }
    
    /* 
	Getting item's plugin in order to access item body, pointer stored 
	somewhere in.
    */
    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find internal item plugin.");
	return 0;
    }

    /* Getting internal item body */
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    /* Calluing node plugin for servicing the request */
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	get_pointer, body);
}

/* Checks is specifid item has passed node pointer */
int reiserfs_node_has_pointer(
    reiserfs_node_t *node,	/* node to be inspected */
    uint32_t pos,		/* pos internal item lies */
    blk_t blk			/* pointer to be checked */
) {
    void *body;
    reiserfs_plugin_t *plugin;
  
    aal_assert("umka-607", node != NULL, return 0);

    /* Checking if specified item isn't an internal item */
    if (!reiserfs_node_item_internal(node, pos))
	return 0;

    /* 
	Getting item's plugin in order to access item body, pointer stored 
	somewhere in.
    */
    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
   
    /* Getting item body */
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return 0;
    }
    
    /* Calling the plugin */
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	has_pointer, body, blk);
}

/* Checks whether item at passed pos is an internal item */
int reiserfs_node_item_internal(
    reiserfs_node_t *node,	/* node to be inspected */
    uint32_t pos		/* item pos to be checked */
) {
    aal_assert("vpf-042", node != NULL, return 0);
    return reiserfs_node_item_get_pid(node, pos) == REISERFS_INTERNAL_ITEM;
}

#ifndef ENABLE_COMPACT

/* Updates item plugin id */
errno_t reiserfs_node_item_set_pid(
    reiserfs_node_t *node,	/* node to be used */
    uint32_t pos,		/* item pos pid will be updated in */
    reiserfs_id_t pid		/* new plugin id */
) {
    aal_assert("umka-551", node != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	item_set_pid, node->entity, pos, pid);
}

/* Updates node pointer in internal item specified by "pos" */
errno_t reiserfs_node_set_pointer(
    reiserfs_node_t *node,	/* node to be used for working with */
    uint32_t pos,		/* internal item pos */
    blk_t blk			/* new pointer */
) {
    void *body;
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return -1);

    /* Checking if specified item is an internal item */
    if (!reiserfs_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to set the node pointer inside non-internal item.");
	return -1;
    }
    
    /* Getting needed plugin */
    if (!(plugin = reiserfs_node_item_get_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }
    
    /* Getting item body for using it with plugin */
    if (!(body = reiserfs_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos);
	return -1;
    }
    
    /* Calling node plugin for handling */
    return libreiser4_plugin_call(return -1, plugin->item_ops.specific.internal, 
	set_pointer, body, blk);
}

/*
    We can estimate size for insertion and for pasting of hint->data (to be memcpy) 
    or of item_info->info (data to be created on the base of).
    
    1. Insertion of data: 
    a) pos->unit == 0xffff 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    2. Insertion of info: 
    a) pos->unit == 0xffff 
    b) hint->info != NULL
    c) hint->plugin != NULL
    
    3. Pasting of data: 
    a) pos->unit != 0xffff 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    4. Pasting of info: 
    a) pos->unit_pos != 0xffff 
    b) hint->info != NULL
    c) get hint->plugin on the base of pos.
*/

errno_t reiserfs_node_item_estimate(
    reiserfs_node_t *node,	/* node to be used for working with */
    reiserfs_pos_t *pos,	/* pos of item/unit to be estimated */
    reiserfs_item_hint_t *item	/* item hint to be estimated */
) {
    aal_assert("vpf-106", item != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", pos != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", pos->unit != 0xffff || 
	item->plugin != NULL, return -1);
   
    if (!item->plugin && !(item->plugin = 
	reiserfs_node_item_get_plugin(node, pos->item))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }

    /* Here hint has been already set for the 3rd case */
    if (item->data != NULL)
	return 0;
    
    /* Estimate for the 2nd and for the 4th cases */
    return libreiser4_plugin_call(return -1, item->plugin->item_ops.common, 
	estimate, pos->unit, item);
}

#endif

