/*
    librepair/node.c - methods are needed for node recovery.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/

#include <repair/librepair.h>
#include <reiser4/reiser4.h>

errno_t repair_node_check(reiser4_node_t *node, void *data) {
    reiser4_key_t key;
    repair_traverse_data_t *traverse = data;
        
    aal_assert("vpf-183", traverse != NULL, return -1);
    aal_assert("vpf-184", traverse->format != NULL, return -1);
    
/*    if (!reiser4_format_data_block(traverse->format, aal_block_get_nr(node->block))) {
	aal_exception_error("The node in the invalid block number (%llu) found in the "
	    "tree.", aal_block_get_nr(node->block));
	return -1;
    }*/
    
    /* Check if we met it already in the control allocator. */
    if (reiser4_alloc_test(traverse->a_control, aal_block_get_nr(node->block))) {
	aal_exception_error("The node in the block (%llu) is used more then once in "
	    "the tree.", aal_block_get_nr(node->block));
	return -1;
    }
    
    if (reiser4_node_lkey(node, &key)) {
	aal_exception_error("Failed to get the left key from the node (%llu).", 
	    aal_block_get_nr(node->block));
	return -1;
    }
    
    if (!(key.plugin = libreiser4_factory_find_by_id(KEY_PLUGIN_TYPE, KEY_REISER40_ID)))
	libreiser4_factory_failed(return -1, find, key, KEY_REISER40_ID);
    
    if (reiser4_key_compare(&traverse->ld_key, &key) != 0) {
	aal_exception_error("The first key %k in the node (%llu) is not equal to the left "
	    "delimiting key %k.", &key, aal_block_get_nr(node->block), &traverse->ld_key);
	return -1;
    }
    
    return 0;
}

errno_t repair_node_update_internal(reiser4_node_t *node, uint32_t pos, void *data) {
    return 0;
}
