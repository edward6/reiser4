/*
    node.c -- reiserfs formated node code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

reiserfs_node_t *reiserfs_node_open(aal_block_t *block) {
    reiserfs_node_t *node;
    
    ASSERT (block != NULL, return NULL);
    ASSERT (block->data != NULL, return NULL);
    
    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    if (!(node->plugin = reiserfs_plugin_find (REISERFS_JOURNAL_PLUGIN, 
			reiserfs_get_node_plugin_id (block)))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "vpf-001", 
	    "Node plugin cannot be find by its identifier %x.",  
	    reiserfs_get_node_plugin_id(block));
	goto error_free_node;
    }

    reiserfs_plugin_check_routine (node->plugin->node, open, goto error_free_node);

    if (!(node->entity = node->plugin->node.open (block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "vpf-002", 
		"Node plugin hasn't been able to open a node %d.", 
		aal_block_location (block));
	goto error_free_node;
    }

    return node;
    
error_free_node:
    aal_free (node);
    return NULL;
}

reiserfs_node_t *reiserfs_node_create(aal_block_t *block, /* allocated block */
				      uint16_t plugin_id, /* node plugin id to be used */
				      uint32_t level)	    /* level of the node in the tree */
{
    reiserfs_node_t *node;
 
    ASSERT (block != NULL, return NULL);
    ASSERT (block->data != NULL, return NULL);

    if (!(node = aal_calloc(sizeof(*node), 0)))
	return NULL;

    reiserfs_node_set_plugin_id (block, plugin_id);
    
    reiserfs_plugin_check_routine (node->plugin->node, create, goto error_free_node);

    if (!(node->entity = node->plugin->node.create (block, level))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "vpf-002", 
		"Node plugin hasn't been able to create a node on block %d.", 
		aal_block_location (block));
	goto error_free_node;
    }

    return node;

error_free_node:
    aal_free (node);    
    return NULL;
}

void reiserfs_node_close(reiserfs_node_t *node, int sync) {
    ASSERT (node != NULL, return);
    
    reiserfs_plugin_check_routine (node->plugin->node, close, goto error_free_node); 

    node->plugin->node.close (node->entity, 1);
   
error_free_node:
    aal_free (node);
}


/* 0 if no problem */
int reiserfs_node_check(reiserfs_node_t *node, int flags) {
    return 0;
}

uint32_t reiserfs_node_max_item_size(reiserfs_node_t *node) {
    ASSERT (node != NULL, return 0);
    
    reiserfs_plugin_check_routine (node->plugin->node, max_item_size, return 0); 

    node->plugin->node.max_item_size (node->entity);

error_exit:
    aal_free (node);
    return 0;
}
    
uint32_t reiserfs_node_max_item_num(reiserfs_node_t *node) {
    return 0;
}

uint32_t reiserfs_node_count(reiserfs_node_t *node) {
    return 0;
}

uint32_t reiserfs_node_level(reiserfs_node_t *node) {
    reiserfs_plugin_check_routine(node->plugin->node, level, return 0);
    return node->plugin->node.level(node->entity);
}

uint32_t reiserfs_node_free_space(reiserfs_node_t *node) {
    return 0;
}

void reiserfs_node_set_free_space(reiserfs_node_t *node) {
}

