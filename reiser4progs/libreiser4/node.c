/*
    node.c -- reiser4 formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

#ifndef ENABLE_COMPACT

/* Creates node on specified device and block and with spcified key plugin */
reiser4_node_t *reiser4_node_create(
    aal_block_t *block,		/* block new node will be created on */
    reiser4_id_t pid,		/* node plugin id to be used */
    uint16_t level		/* node level */
) {
    reiser4_node_t *node;
    
    aal_assert("umka-121", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->block = block;
    
    /* Finding the node plugin by its id */
    if (!(node->plugin = libreiser4_factory_find_by_id(NODE_PLUGIN_TYPE, pid))) 
	libreiser4_factory_failed(goto error_free_node, find, node, pid);

    /* Requesting the plugin for initialization of the entity */
    if (!(node->entity = libreiser4_plugin_call(goto error_free_node, 
	node->plugin->node_ops, create, node->block, level))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create node entity.");
	goto error_free_node;
    }
    
    return node;
    
error_free_node:    
    aal_free(node);
    return NULL;
}

/* Saves specified node to its device */
errno_t reiser4_node_sync(
    reiser4_node_t *node	/* node to be save */
) {
    aal_assert("umka-798", node != NULL, return -1);
    return aal_block_write(node->block);
}

/* Updates key in specified position */
errno_t reiser4_node_set_key(
    reiser4_node_t *node,	/* node to be updated */
    reiser4_pos_t *pos,	/* pos key will be updated in */
    reiser4_key_t *key		/* key to be used */
) {
    aal_assert("umka-804", node != NULL, return -1);
    aal_assert("umka-805", key != NULL, return -1);
    aal_assert("umka-938", pos != NULL, return -1);

    libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	set_key, node->entity, pos, key);
    
    return 0;
}

#endif

/* This function is trying to detect node plugin */
static reiser4_plugin_t *reiser4_node_guess(
    aal_block_t *block		/* block node lies in */
) {
    reiser4_id_t pid;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-902", block != NULL, return NULL);
    
    pid = *((uint16_t *)block->data);
    
    /* Finding node plugin by its id from node header */
    if (!(plugin = libreiser4_factory_find_by_id(NODE_PLUGIN_TYPE, pid))) {
	/* FIXME-UMKA: Here will be further guessing code */
    }

    return plugin;
}

/* Opens node on specified device and block number */
reiser4_node_t *reiser4_node_open(
    aal_block_t *block		/* block node will be opened on */
) {
    reiser4_node_t *node;

    aal_assert("umka-160", block != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    node->block = block;
    
    /* Finding the node plugin by its id */
    if (!(node->plugin = reiser4_node_guess(node->block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't guess node plugin for node %llu.", 
	    aal_block_get_nr(block));
	return NULL;
    }
    
    /* 
	Initializing node's entity by means of calling "open" method of node
       	plugin.
    */
    if (!(node->entity = libreiser4_plugin_call(goto error_free_node, 
	node->plugin->node_ops, open, node->block)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialize node entity.");
	goto error_free_node;
    }
	    
    return node;
    
error_free_node:
    aal_free(node);
    return NULL;
}

/* Closes specified node */
errno_t reiser4_node_close(reiser4_node_t *node) {
    aal_assert("umka-824", node != NULL, return -1);
    aal_assert("umka-903", node->entity != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops,
	close, node->entity);
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

/* Gets right delimiting key from the specified node */
errno_t reiser4_node_rdkey(
    reiser4_node_t *node,	/* node the rdkey will be obtained from */
    reiser4_key_t *key		/* key pointer to store the found rdkey */
) {
    reiser4_pos_t pos;
    
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);
    
    reiser4_pos_init(&pos, reiser4_node_count(node) - 1, ~0ul);
    reiser4_node_get_key(node, &pos, key);
    
    return 0;
}

/* Gets left delemiting key from the specified node */
errno_t reiser4_node_ldkey(
    reiser4_node_t *node,	/* node the ldkey will be obtained from */
    reiser4_key_t *key		/* key pointer found key will be stored in */
) {
    reiser4_pos_t pos;
    
    aal_assert("umka-753", node != NULL, return -1);
    aal_assert("umka-754", key != NULL, return -1);

    reiser4_pos_init(&pos, 0, ~0ul);
    reiser4_node_get_key(node, &pos, key);
    
    return 0;
}

#ifndef ENABLE_COMPACT

/* 
    Relocates item from position specified by src_pos into position specified by
    dst_pos params. This function is used for cpying and moving items by balancing 
    code.
*/
static errno_t reiser4_node_relocate(
    reiser4_node_t *dst_node,	/* destination node */
    reiser4_pos_t *dst_pos,	/* destination pos in destination node */
    reiser4_node_t *src_node,	/* source node */
    reiser4_pos_t *src_pos,	/* source position in source node */
    int remove			/* whether moved ite mshould be removed in src node */
) {
    reiser4_id_t pid;
    reiser4_item_hint_t item;

    aal_assert("umka-799", src_node != NULL, return -1);
    aal_assert("umka-800", dst_node != NULL, return -1);

    item.data = reiser4_node_item_body(src_node, src_pos);
    item.len = reiser4_node_item_len(src_node, src_pos);
    
    /* Getting the key of item that is going to be copied */
    reiser4_node_get_key(src_node, src_pos, (reiser4_key_t *)&item.key);
    
    pid = reiser4_node_item_get_pid(src_node, src_pos);
	
    if (!(item.plugin = libreiser4_factory_find_by_id(ITEM_PLUGIN_TYPE, pid)))
        libreiser4_factory_failed(return -1, find, item, pid);

    /* Insering the item into new location */
    if (reiser4_node_insert(dst_node, dst_pos, &item))
        return -1;
    
    /* Remove src item if remove flag is turned on */
    if (remove && reiser4_node_remove(src_node, src_pos))
        return -1;
    
    return 0;
}

/* 
    Wrapper for reiser4_node_relocate function. This function actually copies
    item specified by src* params to dst* location. Parametrs meaning the same 
    as in reiser4_node_relocate.
*/
errno_t reiser4_node_copy(reiser4_node_t *dst_node, 
    reiser4_pos_t *dst_pos, reiser4_node_t *src_node, 
    reiser4_pos_t *src_pos) 
{
    return reiser4_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 0);
}

/* 
    Wrapper for reiser4_node_relocate function. This function actually moves
    item specified by src* params to dst* location. Parameters meaning the same 
    as in previous one case.
*/
errno_t reiser4_node_move(reiser4_node_t *dst_node, 
    reiser4_pos_t *dst_pos, reiser4_node_t *src_node, 
    reiser4_pos_t *src_pos) 
{
    return reiser4_node_relocate(dst_node, dst_pos, 
	src_node, src_pos, 1);
}

/* 
    Splits node by means of moving right half of node into specified "right" node.
    This function is used by balancing code for splitting the internal nodes in 
    the case target internal node doesn't has enought free space for new node pointer.
*/
errno_t reiser4_node_split(
    reiser4_node_t *node,	/* node to be splitted */
    reiser4_node_t *right	/* node right half of splitted node will be stored */
) {
    uint32_t median;
    reiser4_pos_t dst_pos, src_pos;
    
    aal_assert("umka-780", node != NULL, return -1);
    aal_assert("umka-781", right != NULL, return -1);

    median = reiser4_node_count(node) / 2;
    while (reiser4_node_count(node) > median) {

	reiser4_pos_init(&dst_pos, 0, ~0ul);
        reiser4_pos_init(&src_pos, reiser4_node_count(node) - 1, ~0ul);
	
	if (reiser4_node_move(right, &dst_pos, node, &src_pos))
	    return -1;
    }
    
    return 0;
}

/* Checks node for validness */
errno_t reiser4_node_valid(
    reiser4_node_t *node,	/* node to be checked */
    int flags			/* some flags (not used at the moment) */
) {
    aal_assert("umka-123", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	valid, node->entity, flags);
}

#endif

int reiser4_node_confirm(reiser4_node_t *node) {
    aal_assert("umka-123", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	confirm, node->entity);
}

/* 
    This function makes lookup inside specified node in order to find item/unit 
    stored in it.
*/
int reiser4_node_lookup(
    reiser4_node_t *node,	/* node to be grepped */
    reiser4_key_t *key,	/* key to be find */
    reiser4_pos_t *pos		/* found pos will be stored here */
) {
    int lookup; 
    reiser4_key_t maxkey;
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);

    reiser4_pos_init(pos, 0, ~0ul);

    if (reiser4_node_count(node) == 0)
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

    if (!(plugin = reiser4_node_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. Key could lies 
	within the item or after the item.
    */
    reiser4_node_get_key(node, pos, &maxkey);
    
    if (plugin->item_ops.common.maxkey) {
	    
	if (plugin->item_ops.common.maxkey(&maxkey) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_get_nr(node->block));
	    return -1;
	}
	
    }

    if (reiser4_key_compare(key, &maxkey) > 0) {
        pos->item++;
        return 0;
    }
    
    /* Calling lookup method of found item (most probably direntry item) */
    if (!plugin->item_ops.common.lookup)
	return 0;
	    
    if (!(body = reiser4_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item at node %llu and pos %u.", 
	    aal_block_get_nr(node->block), pos->item);
	return -1;
    }
    
    if ((lookup = plugin->item_ops.common.lookup(body, key, 
	&pos->unit)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    pos->item, aal_block_get_nr(node->block));
	return -1;
    }

    return lookup;
}

/* Returns real item count in specified node */
uint32_t reiser4_node_count(reiser4_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	count, node->entity);
}

#ifndef ENABLE_COMPACT

/* Removes specified by pos item from node */
errno_t reiser4_node_remove(
    reiser4_node_t *node,	/* node item will be removed from */
    reiser4_pos_t *pos		/* position item will be removed at */
) {
    aal_assert("umka-767", node != NULL, return -1);
    aal_assert("umka-768", pos != NULL, return -1);

    if (pos->unit == ~0ul)
	return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	    remove, node->entity, pos);
    else
	return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	    cut, node->entity, pos);
}

/* Inserts item described by item hint into specified node at specified pos */
errno_t reiser4_node_insert(
    reiser4_node_t *node,	/* node new item will be inserted in */
    reiser4_pos_t *pos,	/* position new item will be inserted at */
    reiser4_item_hint_t *item	/* item hint */
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
	if (item->len == 0 && reiser4_node_item_estimate(node, pos, item)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't estimate space that item being inserted will consume.");
	    return -1;
	}
    } else {
	aal_assert("umka-761", item->len > 0 && 
	    item->len < node->block->size, return -1);
    }
    
    /* Checking if item length is gretter then free space in node */
    if (item->len + (pos->unit == ~0ul ? reiser4_node_item_overhead(node) : 0) >
        reiser4_node_get_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the %s of (%u) size in the node (%llu).",
            (pos->unit == ~0ul ? "item" : "unit"), item->len, 
	    aal_block_get_nr(node->block));
        return -1;
    }

    /* 
	Inserting new item or passting unit into one existent item pointed by 
	pos->item.
    */
    if (pos->unit == ~0ul) {
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
errno_t reiser4_node_set_pid(
    reiser4_node_t *node,	/* node to be updated */
    uint32_t pid		/* node plugin id to be used */
) {
    aal_assert("umka-828", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops,
	set_pid, node->entity, pid);
}

/* Sets node's level */
errno_t reiser4_node_set_level(
    reiser4_node_t *node,	/* node new level will be set */
    uint8_t level		/* level will be used */
) {
    aal_assert("umka-454", node != NULL, return -1);

    if (node->plugin->node_ops.set_level == NULL)
        return 0;

    return node->plugin->node_ops.set_level(node->entity, level);
}

/* Sets node free space */
errno_t reiser4_node_set_space(
    reiser4_node_t *node,	/* node to be updated */
    uint32_t value		/* free space to be set */
) {
    aal_assert("umka-456", node != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	set_space, node->entity, value);
}

#endif

/* Returns node plugin id in use */
uint32_t reiser4_node_get_pid(
    reiser4_node_t *node	/* node pid to be obtained */
) {
    aal_assert("umka-828", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops,
	get_pid, node->entity);
}

/* Returns level of specified node */
uint8_t reiser4_node_get_level(reiser4_node_t *node) {
    aal_assert("umka-539", node != NULL, return 0);
    
    if (node->plugin->node_ops.get_level == NULL)
        return 0;

    return node->plugin->node_ops.get_level(node->entity);
}

/* Returns free space of specified node */
uint32_t reiser4_node_get_space(reiser4_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	get_space, node->entity);
}

/* Returns overhead of specified node */
uint32_t reiser4_node_item_overhead(reiser4_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_overhead,);
}

/* Returns item max size from in specified node */
uint32_t reiser4_node_item_maxsize(reiser4_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_maxsize, node->entity);
}

/* Returns length of specified item */
uint32_t reiser4_node_item_len(
    reiser4_node_t *node,	/* node length of specified item wuill be obtained from */
    reiser4_pos_t *pos		/* item position */
) {
    aal_assert("umka-760", node != NULL, return 0);
    aal_assert("umka-945", pos != NULL, return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_len, node->entity, pos);
}

/* Returns body pointer of speficied item */
reiser4_body_t *reiser4_node_item_body(
    reiser4_node_t *node,	/* node item body will be obtained from */
    reiser4_pos_t *pos		/* item pos body will be obtained at */
) {
    aal_assert("umka-554", node != NULL, return NULL);
    aal_assert("umka-946", pos != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, node->plugin->node_ops, 
	item_body, node->entity, pos);
}

/* Returns key from specified node at sepcified pos */
errno_t reiser4_node_get_key(
    reiser4_node_t *node,	/* node key will be got from */
    reiser4_pos_t *pos,	/* pos key will be got at */
    reiser4_key_t *key		/* place found key will be stored in */
) {
    errno_t res;
    
    aal_assert("umka-565", node != NULL, return -1);
    aal_assert("umka-803", key != NULL, return -1);
    aal_assert("umka-947", pos != NULL, return -1);
    
    if ((res = libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	    get_key, node->entity, pos, key)))
	return res;

    return -((key->plugin = reiser4_key_guess(&key->body)) == NULL);
}

/* Returns item plugin id */
reiser4_id_t reiser4_node_item_get_pid(
    reiser4_node_t *node,	/* node to be inspected */
    reiser4_pos_t *pos		/* item pos plugin id will be obtained from */
) {
    aal_assert("vpf-047", node != NULL, return 0);
    aal_assert("umka-948", pos != NULL, return 0);
    
    aal_assert("umka-904", pos->item < reiser4_node_count(node), return 0);

    return libreiser4_plugin_call(return 0, node->plugin->node_ops, 
	item_get_pid, node->entity, pos);
}

/* Returns item plugin by item pos */
reiser4_plugin_t *reiser4_node_item_plugin(
    reiser4_node_t *node,	/* node plugin will be got from */
    reiser4_pos_t *pos		/* item pos plugin will be obtained at */
) {
    aal_assert("umka-755", node != NULL, return NULL);
    aal_assert("umka-949", pos != NULL, return NULL);
    
    return libreiser4_factory_find_by_id(ITEM_PLUGIN_TYPE, 
	reiser4_node_item_get_pid(node, pos));
}

/* Returns node pointer from internal node */
blk_t reiser4_node_get_pointer(
    reiser4_node_t *node,	/* node pointer will be obtained from */
    reiser4_pos_t *pos		/* item position to be used */
) {
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
    
    aal_assert("vpf-041", node != NULL, return 0);
    aal_assert("umka-950", pos != NULL, return 0);
    
    aal_assert("umka-778", pos->item < reiser4_node_count(node), return 0);

    /* Checking if specified item isn't an internal item */
    if (!reiser4_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to get the node pointer from non-internal item.");
	return 0;
    }
    
    /* 
	Getting item's plugin in order to access item body, pointer stored 
	somewhere in.
    */
    if (!(plugin = reiser4_node_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find internal item plugin.");
	return 0;
    }

    /* Getting internal item body */
    if (!(body = reiser4_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos->item);
	return 0;
    }
    
    /* Calluing node plugin for servicing the request */
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	get_pointer, body);
}

/* Checks is specifid item has passed node pointer */
int reiser4_node_has_pointer(
    reiser4_node_t *node,	/* node to be inspected */
    reiser4_pos_t *pos,	/* pos internal item lies */
    blk_t blk			/* pointer to be checked */
) {
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
  
    aal_assert("umka-607", node != NULL, return 0);
    aal_assert("umka-951", pos != NULL, return 0);

    /* Checking if specified item isn't an internal item */
    if (!reiser4_node_item_internal(node, pos))
	return 0;

    /* 
	Getting item's plugin in order to access item body, pointer stored 
	somewhere in.
    */
    if (!(plugin = reiser4_node_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
   
    /* Getting item body */
    if (!(body = reiser4_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos->item);
	return 0;
    }
    
    /* Calling the plugin */
    return libreiser4_plugin_call(return 0, plugin->item_ops.specific.internal, 
	has_pointer, body, blk);
}

/* Checks whether item at passed pos is an internal item */
int reiser4_node_item_internal(
    reiser4_node_t *node,	/* node to be inspected */
    reiser4_pos_t *pos		/* item pos to be checked */
) {
    reiser4_plugin_t *plugin;
    
    aal_assert("vpf-042", node != NULL, return 0);
    aal_assert("umka-952", pos != NULL, return 0);

    if (!(plugin = reiser4_node_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return 0;
    }
    
    return plugin->item_ops.common.internal && 
	plugin->item_ops.common.internal();
}

#ifndef ENABLE_COMPACT

/* Updates item plugin id */
errno_t reiser4_node_item_set_pid(
    reiser4_node_t *node,	/* node to be used */
    reiser4_pos_t *pos,	/* item pos pid will be updated in */
    reiser4_id_t pid		/* new plugin id */
) {
    aal_assert("umka-551", node != NULL, return -1);
    aal_assert("umka-953", pos != NULL, return -1);

    return libreiser4_plugin_call(return -1, node->plugin->node_ops, 
	item_set_pid, node->entity, pos, pid);
}

/* Updates node pointer in internal item specified by "pos" */
errno_t reiser4_node_set_pointer(
    reiser4_node_t *node,	/* node to be used for working with */
    reiser4_pos_t *pos,	/* internal item pos */
    blk_t blk			/* new pointer */
) {
    reiser4_body_t *body;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-607", node != NULL, return -1);
    aal_assert("umka-954", pos != NULL, return -1);

    /* Checking if specified item is an internal item */
    if (!reiser4_node_item_internal(node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "An attempt to set the node pointer inside non-internal item.");
	return -1;
    }
    
    /* Getting needed plugin */
    if (!(plugin = reiser4_node_item_plugin(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find item plugin.");
	return -1;
    }
    
    /* Getting item body for using it with plugin */
    if (!(body = reiser4_node_item_body(node, pos))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find item at node %llu and pos %u",
	    aal_block_get_nr(node->block), pos->item);
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
    a) pos->unit == ~0ul 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    2. Insertion of info: 
    a) pos->unit == ~0ul 
    b) hint->info != NULL
    c) hint->plugin != NULL
    
    3. Pasting of data: 
    a) pos->unit != ~0ul 
    b) hint->data != NULL
    c) get hint->plugin on the base of pos.
    
    4. Pasting of info: 
    a) pos->unit_pos != ~0ul 
    b) hint->info != NULL
    c) get hint->plugin on the base of pos.
*/

errno_t reiser4_node_item_estimate(
    reiser4_node_t *node,	/* node to be used for working with */
    reiser4_pos_t *pos,	/* pos of item/unit to be estimated */
    reiser4_item_hint_t *item	/* item hint to be estimated */
) {
    aal_assert("vpf-106", item != NULL, return -1);
    aal_assert("umka-541", node != NULL, return -1);
    aal_assert("umka-604", pos != NULL, return -1);

    /* We must have hint->plugin initialized for the 2nd case */
    aal_assert("vpf-118", pos->unit != ~0ul || 
	item->plugin != NULL, return -1);
   
    if (!item->plugin && !(item->plugin = reiser4_node_item_plugin(node, pos))) {
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

