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
    rpid_t pid,		/* node plugin id to be used */
    uint8_t level		/* node level */
) {
    reiser4_node_t *node;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-121", block != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
    
    node->block = block;
    
    /* Finding the node plugin by its id */
    if (!(plugin = libreiser4_factory_ifind(NODE_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find node plugin by its id 0x%x.", pid);
	goto error_free_node;
    }
    
    /* Requesting the plugin for initialization of the entity */
    if (!(node->entity = plugin_call(goto error_free_node, 
	plugin->node_ops, create, node->block, level))) 
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
    return aal_block_sync(node->block);
}

/* Updates key in specified position */
errno_t reiser4_node_set_key(
    reiser4_node_t *node,	/* node to be updated */
    reiser4_pos_t *pos,		/* pos key will be updated in */
    reiser4_key_t *key		/* key to be used */
) {
    aal_assert("umka-804", node != NULL, return -1);
    aal_assert("umka-805", key != NULL, return -1);
    aal_assert("umka-938", pos != NULL, return -1);

    plugin_call(return -1, node->entity->plugin->node_ops, 
	set_key, node->entity, pos, key);
    
    return 0;
}

#endif

/* This function is trying to detect node plugin */
static reiser4_plugin_t *reiser4_node_guess(
    aal_block_t *block		/* block node lies in */
) {
    rpid_t pid;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-902", block != NULL, return NULL);
    
    pid = *((uint16_t *)block->data);
    
    /* Finding node plugin by its id from node header */
    if (!(plugin = libreiser4_factory_ifind(NODE_PLUGIN_TYPE, pid))) {
	/* FIXME-UMKA: Here will be further guessing code */
    }

    return plugin;
}

/* Opens node on specified device and block number */
reiser4_node_t *reiser4_node_open(
    aal_block_t *block		/* block node will be opened on */
) {
    reiser4_node_t *node;
    reiser4_plugin_t *plugin;

    aal_assert("umka-160", block != NULL, return NULL);
   
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;
   
    node->block = block;
    
    /* Finding the node plugin by its id */
    if (!(plugin = reiser4_node_guess(node->block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't guess node plugin for node %llu.", 
	    aal_block_number(block));
	return NULL;
    }
    
    /* 
	Initializing node's entity by means of calling "open" method of node
       	plugin.
    */
    if (!(node->entity = plugin_call(goto error_free_node, 
	plugin->node_ops, open, node->block)))
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
    
    return plugin_call(return -1, node->entity->plugin->node_ops,
	close, node->entity);
    
    aal_block_free(node->block);
    aal_free(node);

    return 0;
}

/* Gets right delimiting key from the specified node */
errno_t reiser4_node_rkey(
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
errno_t reiser4_node_lkey(
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
    reiser4_item_t item;
    reiser4_item_hint_t hint;

    aal_assert("umka-799", src_node != NULL, return -1);
    aal_assert("umka-800", dst_node != NULL, return -1);

    if (reiser4_item_open(&item, src_node, src_pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open item by its coord. Node %llu, item %u.",
	    aal_block_number(src_node->block), src_pos->item);
	return -1;
    }
    
    hint.len = reiser4_item_len(&item);
    hint.data = reiser4_item_body(&item);
    hint.plugin = reiser4_item_plugin(&item);
    
    /* Getting the key of item that is going to be copied */
    reiser4_node_get_key(src_node, src_pos, (reiser4_key_t *)&hint.key);

    /* Insering the item into new location */
    if (reiser4_node_insert(dst_node, dst_pos, &hint))
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
    reiser4_node_t *node	/* node to be checked */
) {
    aal_assert("umka-123", node != NULL, return -1);
    
    return plugin_call(return -1, node->entity->plugin->node_ops, 
	valid, node->entity);
}

#endif

int reiser4_node_confirm(reiser4_node_t *node) {
    aal_assert("umka-123", node != NULL, return 0);
    
    return plugin_call(return 0, node->entity->plugin->node_ops, 
	confirm, node->block);
}

/* 
    This function makes lookup inside specified node in order to find item/unit 
    stored in it.
*/
int reiser4_node_lookup(
    reiser4_node_t *node,	/* node to be grepped */
    reiser4_key_t *key,		/* key to be find */
    reiser4_pos_t *pos		/* found pos will be stored here */
) {
    int lookup; 
    reiser4_item_t item;
    reiser4_key_t maxkey;
    
    aal_assert("umka-475", pos != NULL, return -1);
    aal_assert("vpf-048", node != NULL, return -1);
    aal_assert("umka-476", key != NULL, return -1);

    reiser4_pos_init(pos, 0, ~0ul);

    if (reiser4_node_count(node) == 0)
	return 0;
   
    /* Calling node plugin */
    if ((lookup = plugin_call(return -1, 
	node->entity->plugin->node_ops, lookup, node->entity, key, pos)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the node %llu failed.", 
	    aal_block_number(node->block));
	return -1;
    }

    if (lookup == 1) return 1;

    if (reiser4_item_open(&item, node, pos)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open item by coord. Nosde %llu, item %u.",
	    aal_block_number(node->block), pos->item);
	return -1;
    }

    /*
	We are on the position where key is less then wanted. Key could lies 
	within the item or after the item.
    */
    reiser4_node_get_key(node, pos, &maxkey);
    
    if (item.plugin->item_ops.common.maxkey) {
	    
	if (item.plugin->item_ops.common.maxkey(&maxkey) == -1) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Getting max key of the item %d in the node %llu failed.", 
		pos->item, aal_block_number(node->block));
	    return -1;
	}
	
    }

    if (reiser4_key_compare(key, &maxkey) > 0) {
        pos->item++;
        return 0;
    }
    
    /* Calling lookup method of found item (most probably direntry item) */
    if (!item.plugin->item_ops.common.lookup)
	return 0;
	    
    if ((lookup = item.plugin->item_ops.common.lookup(item.body, key, 
	&pos->unit)) == -1) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Lookup in the item %d in the node %llu failed.", 
	    pos->item, aal_block_number(node->block));
	return -1;
    }

    return lookup;
}

/* Returns real item count in specified node */
uint32_t reiser4_node_count(reiser4_node_t *node) {
    aal_assert("umka-453", node != NULL, return 0);
    
    return plugin_call(return 0, node->entity->plugin->node_ops, 
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
	return plugin_call(return -1, node->entity->plugin->node_ops, 
	    remove, node->entity, pos);
    else
	return plugin_call(return -1, node->entity->plugin->node_ops, 
	    cut, node->entity, pos);
}

/* Inserts item described by item hint into specified node at specified pos */
errno_t reiser4_node_insert(
    reiser4_node_t *node,	/* node new item will be inserted in */
    reiser4_pos_t *pos,		/* position new item will be inserted at */
    reiser4_item_hint_t *hint	/* item hint */
) {
    errno_t ret;
    
    aal_assert("vpf-111", node != NULL, return -1);
    aal_assert("vpf-110", hint != NULL, return -1);
    aal_assert("vpf-108", pos != NULL, return -1);

    if (!hint->data) {
	/* 
	    Estimate the size that will be spent for item. This should be done
	    if item->data not installed.
	*/
	if (hint->len == 0) {
	    reiser4_item_t item;
	    
	    reiser4_item_init(&item, node, pos);
	    
	    if (reiser4_item_estimate(&item, hint)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		    "Can't estimate space that item being inserted "
		    "will consume.");
		return -1;
	    }
	}
    } else {
	aal_assert("umka-761", hint->len > 0 && 
	    hint->len < reiser4_node_maxspace(node), return -1);
    }
    
    /* Checking if item length is gretter then free space in node */
    if (hint->len + (pos->unit == ~0ul ? reiser4_node_overhead(node) : 0) >
        reiser4_node_space(node))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
            "There is no space to insert the %s of (%u) size in the node (%llu).",
            (pos->unit == ~0ul ? "item" : "unit"), hint->len, 
	    aal_block_number(node->block));
        return -1;
    }

    /* 
	Inserting new item or passting unit into one existent item pointed by 
	pos->item.
    */
    if (pos->unit == ~0ul) {
        if ((ret = plugin_call(return -1, node->entity->plugin->node_ops, 
		insert, node->entity, pos, hint)) != 0)
	    return ret;
    } else {
	if ((ret = plugin_call(return -1, node->entity->plugin->node_ops, 
		paste, node->entity, pos, hint)) != 0)
	    return ret;
    }
    
    return 0;
}

#endif

/* Returns node plugin id in use */
uint16_t reiser4_node_pid(
    reiser4_node_t *node	/* node pid to be obtained */
) {
    aal_assert("umka-828", node != NULL, return 0);
    
    return plugin_call(return 0, node->entity->plugin->node_ops,
	pid, node->entity);
}

/* Returns free space of specified node */
uint16_t reiser4_node_space(reiser4_node_t *node) {
    aal_assert("umka-455", node != NULL, return 0);
    
    return plugin_call(return 0, node->entity->plugin->node_ops, 
	space, node->entity);
}

/* Returns overhead of specified node */
uint16_t reiser4_node_overhead(reiser4_node_t *node) {
    aal_assert("vpf-066", node != NULL, return 0);

    return plugin_call(return 0, node->entity->plugin->node_ops, 
	overhead, node->entity);
}

/* Returns item max size from in specified node */
uint16_t reiser4_node_maxspace(reiser4_node_t *node) {
    aal_assert("umka-125", node != NULL, return 0);
    
    return plugin_call(return 0, node->entity->plugin->node_ops, 
	maxspace, node->entity);
}

/* Returns key from specified node at sepcified pos */
errno_t reiser4_node_get_key(
    reiser4_node_t *node,	/* node key will be got from */
    reiser4_pos_t *pos,		/* pos key will be got at */
    reiser4_key_t *key		/* place found key will be stored in */
) {
    errno_t res;
    
    aal_assert("umka-565", node != NULL, return -1);
    aal_assert("umka-803", key != NULL, return -1);
    aal_assert("umka-947", pos != NULL, return -1);
    
    if ((res = plugin_call(return -1, node->entity->plugin->node_ops, 
	    get_key, node->entity, pos, key)))
	return res;

    /* 
	FIXME-VITALY: when this guess will check key structure, plugin 
	should be set outside here. Fsck should be able to recover such 
	keys but it won't getting -1.
    */
    return -((key->plugin = reiser4_key_guess(&key->body)) == NULL);
}

