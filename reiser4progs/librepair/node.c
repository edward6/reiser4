/*
    librepair/node.c - methods are needed for node recovery.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <repair/librepair.h>
#include <reiser4/reiser4.h>

static errno_t repair_item_open(reiser4_item_t *item, reiser4_node_t *node, 
    reiser4_pos_t *pos)
{
    rpid_t pid;
    
    aal_assert("vpf-232", node != NULL, return -1);
    aal_assert("vpf-235", node->entity != NULL, return -1);
    aal_assert("vpf-236", node->entity->plugin != NULL, return -1);
    aal_assert("vpf-233", pos != NULL, return -1);
    aal_assert("vpf-234", item != NULL, return -1);
    
    /* Check items plugin ids. */
    /* FIXME-VITALY: There will be a fix for plugin ids in a future here. */
    pid = plugin_call(return 0, node->entity->plugin->node_ops, item_pid, 
	node->entity, pos);

    if ((pid == INVALID_PLUGIN_ID) || !(item->plugin = 
	libreiser4_factory_ifind(ITEM_PLUGIN_TYPE, pid))) 
    {
        aal_exception_error("Node (%llu): unknown plugin is specified for "
	    "the item (%u).",aal_block_number(node->block), pos->item);
	return 1;
    }
	    
    if (!(item->body = plugin_call(return -1, node->entity->plugin->node_ops, 
	item_body, node->entity, pos))) 
    {
	aal_exception_error("Node (%llu): Failed to get the item (%u) body.", 
	    aal_block_number(node->block), pos->item);
	return -1;
    }

    item->len = plugin_call(return -1, node->entity->plugin->node_ops, 
	item_len, node->entity, pos);

    item->node = node;
    item->pos = pos;

    return 0;
}

static errno_t repair_node_items_check(reiser4_node_t *node, 
    repair_check_t *data) 
{
    reiser4_item_t item;
    reiser4_pos_t pos;
    rpid_t pid;
    int res;

    aal_assert("vpf-229", node != NULL, return -1);
    aal_assert("vpf-230", node->entity != NULL, return -1);
    aal_assert("vpf-231", node->entity->plugin != NULL, return -1);
    aal_assert("vpf-242", data != NULL, return -1);

    pos.unit = ~0ul;
    for (pos.item = 0; pos.item < reiser4_node_count(node); pos.item++) {
	/* Open the item, checking its plugin id. */
	if ((res = repair_item_open(&item, node, &pos))) {
	    if (res > 0) {
		aal_exception_error("Node (%llu): Failed to open the item (%u)."
		    " Removed.", aal_block_number(node->block), pos.item);
	    
		if (reiser4_node_remove(node, &pos)) {
		    aal_exception_bug("Node (%llu): Failed to delete the item "
			"(%d).", aal_block_number(node->block), pos.item);
		    return -1;
		}
	    } else {
		aal_exception_error("Node (%llu): Failed to open the item (%u).", 
		    aal_block_number(node->block), pos.item);
		return -1;
	    }
	}
	
	/* Check that the item is legal for this node. */
	if ((res = plugin_call(return -1, node->entity->plugin->node_ops, 
	    item_legal, node->entity, item.plugin)))
	    return res;	

	/* Check the item structure. */
	if ((res = plugin_call(return -1, item.plugin->item_ops, check, 
		&item, data->options))) 
	    return res;
    }
    
    return 0;    
}

static errno_t repair_node_dkeys_check(reiser4_node_t *node, 
    repair_check_t *data) 
{
    reiser4_key_t key;

    aal_assert("vpf-248", node != NULL, return -1);
    aal_assert("vpf-249", node->entity != NULL, return -1);
    aal_assert("vpf-250", node->entity->plugin != NULL, return -1);
    aal_assert("vpf-240", data != NULL, return -1);
    aal_assert("vpf-241", data->format != NULL, return -1);

    reiser4_key_minimal(&key);
    if (reiser4_key_compare(&data->ld_key, &key)) {
	if (reiser4_node_lkey(node, &key)) {
	    aal_exception_error("Node (%llu): Failed to get the left key.", 
		aal_block_number(node->block));
	    return 1;
	}
    
	if (reiser4_key_compare(&data->ld_key, &key) != 0) {
	    aal_exception_error("Node (%llu): The first key %k is not equal to "
		"the left delimiting key %k.", &key, 
		aal_block_number(node->block), &data->ld_key);
	    return 1;
	}
    }

    reiser4_key_maximal(&key);
    if (reiser4_key_compare(&key, &data->ld_key)) {
	if (reiser4_node_rkey(node, &key)) {
	    aal_exception_error("Node (%llu): Failed to get the right key.", 
		aal_block_number(node->block));
	    return 1;
	}
    
	if (reiser4_key_compare(&key, &data->rd_key) > 0) {
	    aal_exception_error("Node (%llu): The last key %k is not equal to "
		"the right delimiting key %k.", &key, 
		aal_block_number(node->block), &data->ld_key);
	    return 1;
	}
    }

    return 0;
}

static errno_t repair_node_keys_check(reiser4_node_t *node, 
    repair_check_t *data) 
{
    reiser4_key_t key, prev_key;
    reiser4_pos_t pos = {0, ~0ul};
    errno_t res;
    
    aal_assert("vpf-258", node != NULL, return -1);
    
    if (!(key.plugin = libreiser4_factory_ifind(KEY_PLUGIN_TYPE, 
	KEY_REISER40_ID))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find key plugin by its id 0x%x.", KEY_REISER40_ID);
	return -1;
    }
    
    for (pos.item = 0; reiser4_node_count(node); pos.item++) {
	if (reiser4_node_get_key(node, &pos, &key)) {
	    aal_exception_error("Node (%llu): Failed to get the key of the "
		"item (%u).", aal_block_number(node->block), pos.item);
	    return -1;
	}
	if (reiser4_key_valid(&key)) {
	    aal_exception_error("Node (%llu): The key %k of the item (%u) is "
		"not valid. Item removed.", aal_block_number(node->block), 
		&key, pos.item);
	    
	    if (reiser4_node_remove(node, &pos)) {
		aal_exception_bug("Node (%llu): Failed to delete the item "
		    "(%d).", aal_block_number(node->block), pos.item);
		return -1;
	    }
	}
	if (pos.item) {	    
	    if ((res = reiser4_key_compare(&prev_key, &key)) > 0 || 
		(res == 0 && (reiser4_key_get_type(&key) != KEY_FILENAME_TYPE ||
		reiser4_key_get_type(&prev_key) != KEY_FILENAME_TYPE))) 
	    {
		/* 
		    FIXME-VITALY: Which part does put the rule that neighbour 
		    keys could be equal?
		*/
		return 1;		
	    }
	}
	prev_key = key;
    }
    
    return 0;
}

/* 
    Checks the node content. 
    Returns: 0 - OK; -1 - unrecoverable error; 1 - unrecoverable error;
*/
errno_t repair_node_check(reiser4_node_t *node, repair_check_t *data) {
    blk_t blk;
    int res;
    
    aal_assert("vpf-183", data != NULL, return -1);
    aal_assert("vpf-184", data->format != NULL, return -1);
    aal_assert("vpf-192", node != NULL, return -1);
    aal_assert("vpf-193", node->entity != NULL, return -1);    
    aal_assert("vpf-220", node->entity->plugin != NULL, return -1);

    if (data->level && node->entity->plugin->node_ops.get_level && 
	node->entity->plugin->node_ops.get_level(node->entity) != 
	data->level) 
    {
	aal_exception_error("Level of the node (%u) is not correct, expected "
	    "(%u)", node->entity->plugin->node_ops.get_level(node->entity), 
	    data->level);
	return 1;
    }

    blk = aal_block_number(node->block);
    if ((res = reiser4_format_layout(data->format, callback_data_block_check, 
	&blk))) 
    {
	aal_exception_error("The node in the invalid block number (%llu) "
	    "found in the tree.", aal_block_number(node->block));
	return res;
    }
    
    /* Check if we met it already in the control allocator. */
    if (reiser4_alloc_test(data->a_control, aal_block_number(node->block))) 
    {
	aal_exception_error("The node in the block (%llu) is used more then "
	    "once in the tree.", aal_block_number(node->block));
	return 1;
    }

    if ((res = plugin_call(return -1, node->entity->plugin->node_ops, check, 
	node->entity, data->options)))
	return res;

    if ((res = repair_node_items_check(node, data))) 
	return res;
    
    if ((res = repair_node_keys_check(node, data)))
	    return res;
    
    if ((res = repair_node_dkeys_check(node, data)))
	return res;
    
    if (reiser4_node_count(node) == 0)
	return 1;
	
    return 0;
}

