/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

error_t reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_blk;

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    root_blk = reiserfs_super_get_root(fs);

/*    if (reiserfs_fs_format_plugin_id(fs) == 0x2) {
	if (!(fs->tree->root = reiserfs_node_open(fs->device, root_blk, 0x2)))
	    goto error_free_tree;
    } else {	
	if (!(fs->tree->root = reiserfs_node_open(fs->device, root_blk, REISERFS_GUESS_PLUGIN_ID)))
	    goto error_free_tree;
    }*/
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_tree_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t node_plugin_id) {
    blk_t root_blk;
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_id_t plugin_id;
    
    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("umka-130", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    if (!(root_blk = reiserfs_alloc_find(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't allocate block for root node.");
	goto error_free_tree;
    }
    
    reiserfs_alloc_use(fs, root_blk);
    reiserfs_super_set_root(fs, root_blk);
    
    /* Here will be also allocated leaf node */
    
/*    if (!(fs->tree->root = reiserfs_node_create(fs->device, root_blk, 
	    node_plugin_id, REISERFS_ROOT_LEVEL))) 
	goto error_free_tree;*/
    
    return 0;

    {
	/* create 2 nodes - leaf and squeeze */
	/* create internel item, insert it into squeeze */
	/*  */

	
	/* init internal item to be inserted into tree->root */
	reiserfs_key_t key;
//	reiserfs_sd40_t sd40;
	    
        set_key_type(&key, KEY_SD_MINOR);
        set_key_locality(&key, 1ull);
        set_key_objectid(&key, 2ull);

    }

    
error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-132", fs->tree != NULL, return -1);
    
    return 0/*reiserfs_node_sync(fs->tree->root)*/;
}

#endif

void reiserfs_tree_close(reiserfs_fs_t *fs) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);

//    reiserfs_node_close(fs->tree->root);
    
    aal_free(fs->tree);
    fs->tree = NULL;
}

