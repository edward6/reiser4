/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

int reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_block;
    aal_block_t *block;
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;

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
    
    if (!(fs->tree->root = reiserfs_node_open(block))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-045", 
	    "Can't initialize root node of the tree.");
	goto error_free_block;
    }
    
/*    id = reiserfs_node_plugin(fs->tree->root);
    if (!(plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-039", 
	    "Can't find node plugin for root node by its identifier %x.", id);
	goto error_free_node;
    }
    fs->tree->plugin = plugin;*/

    return 1;

error_free_root:
    reiserfs_node_close(fs->tree->root, 0);
error_free_block:
    aal_block_free(block);
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return 0;
}

int reiserfs_tree_create(reiserfs_fs_t *fs) {
    blk_t root_block;
    aal_block_t *block;
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
    
    if (!(plugin = reiserfs_plugin_find(REISERFS_NODE_PLUGIN, 0x01))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-037", 
	    "Can't find node plugin by its identifier %x.", 0x01);
	goto error_free_tree;
    }
    fs->tree->plugin = plugin;
    
    if (!(block = aal_block_alloc(fs->device, root_block, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-046", 
	    "Can't allocate root block.");
	goto error_free_tree;
    }
    
    if (!(fs->tree->root = reiserfs_node_create(block, REISERFS_ROOT_LEVEL)))
	goto error_free_block;
    
/*    if (!reiserfs_node_sync(fs->tree->root))
	goto error_free_root;*/
    
    return 1;

/*error_free_root:
    reiserfs_node_close(fs->tree->root, 0);*/
error_free_block:
    aal_block_free(block);
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return 0;
}

void reiserfs_tree_close(reiserfs_fs_t *fs, int sync) {
    ASSERT(fs != NULL, return);
    ASSERT(fs->tree != NULL, return);
    
    reiserfs_node_close(fs->tree->root, 1);    

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
}

