/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

/* 
    Tree functions which work with root node will 
    work through node API later. 
*/
int reiserfs_tree_node_check(reiserfs_fs_t *fs, aal_block_t *block) {
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
    reiserfs_node_header_t *header;

    ASSERT(fs != NULL, return 0);
    ASSERT(block != NULL, return 0);

    header = (reiserfs_node_header_t *)block->data;
   
    id = get_nh_plugin_id(header);
    if (!(plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-035", 
	    "Can't find node plugin for root node by its identifier %x.", id);
	return 0;
    }
    
    reiserfs_plugin_check_routine(plugin->node, check, return 0);
    return plugin->node.check(block);
}

int reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_block;
    aal_block_t *block;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
    reiserfs_node_header_t *header;

    ASSERT(fs != NULL, return 0);
    ASSERT(fs->super != NULL, return 0);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return 0;

    if (!(root_block = reiserfs_super_root_block(fs)) || 
	root_block > aal_device_len(fs->device)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-032", 
	    "Invalid root block %d has been detected.", root_block);
	goto error_free_tree;
    }

    if (!(block = aal_block_read(fs->device, root_block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-033", 
	    "Can't read root block %d.", block);
	goto error_free_tree;
    }

    header = (reiserfs_node_header_t *)block->data;
    id = get_nh_plugin_id(header);
    
    if (!(plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-039", 
	    "Can't find node plugin for root node by its identifier %x.", id);
	goto error_free_block;
    }
    fs->tree->plugin = plugin;

    reiserfs_plugin_check_routine(plugin->node, open, goto error_free_block);
    if (!(fs->tree->entity = plugin->node.open(block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-040", 
	    "Can't open node %d.", aal_block_location(block));
	goto error_free_block;
    }
	    
    return 1;

error_free_block:
    aal_block_free(block);
error_free_tree:
    aal_free(fs->tree);
error:
    return 0;
}

int reiserfs_tree_create(reiserfs_fs_t *fs) {
    blk_t root_block;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
    
    ASSERT(fs != NULL, return 0);
    ASSERT(fs->super != NULL, return 0);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return 0;
	
    if (!(root_block = reiserfs_super_root_block(fs)) || 
	root_block > aal_device_len(fs->device)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-036", 
	    "Invalid root block %d has been detected.", root_block);
	goto error_free_tree;
    }
    
    id = reiserfs_super_node_plugin(fs);
    if (!(plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-037", 
	    "Can't find node plugin by its identifier %x.", id);
	goto error_free_tree;
    }
    fs->tree->plugin = plugin;
    
/*    reiserfs_plugin_check_routine(plugin->node, create, goto error_free_tree);
    if (!(fs->tree->entity = plugin->node->create(REISERFS_NODE_LEAF))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-038", 
	    "Can't create root node.");
	goto error_free_tree;
    }*/
    
    return 1;

error_free_tree:
    aal_free(fs->tree);
error:
    return 0;
}

void reiserfs_tree_close(reiserfs_fs_t *fs, int sync) {
    ASSERT(fs != NULL, return);
    ASSERT(fs->tree != NULL, return);
    
    reiserfs_plugin_check_routine(fs->tree->plugin->node, close, return);
    fs->tree->plugin->node.close(fs->tree->entity);
    aal_free(fs->tree);
    fs->tree = NULL;
}

