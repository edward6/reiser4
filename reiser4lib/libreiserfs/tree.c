/*
    tree.c -- reiserfs balanced tree code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>

error_t reiserfs_tree_open(reiserfs_fs_t *fs) {
    blk_t root_blk;

    aal_assert("umka-127", fs != NULL, return -1);
    aal_assert("umka-128", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;

    root_blk = reiserfs_super_root_block(fs);
    
    if (!(fs->tree->root = reiserfs_node_open(fs->device, root_blk)))
        goto error_free_tree;
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

error_t reiserfs_tree_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t node_plugin_id) {
    blk_t root_blk;
    
    aal_assert("umka-129", fs != NULL, return -1);
    aal_assert("umka-130", fs->super != NULL, return -1);

    if (!(fs->tree = aal_calloc(sizeof(*fs->tree), 0)))
	return -1;
	
    root_blk = reiserfs_super_root_block(fs);
    if (!(fs->tree->root = reiserfs_node_create(fs->device, root_blk, 
	    node_plugin_id, REISERFS_ROOT_LEVEL))) 
	goto error_free_tree;
    
    return 0;

error_free_tree:
    aal_free(fs->tree);
    fs->tree = NULL;
error:
    return -1;
}

error_t reiserfs_tree_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-131", fs != NULL, return -1);
    aal_assert("umka-132", fs->tree != NULL, return -1);
    
    return reiserfs_node_sync(fs->tree->root);
}

void reiserfs_tree_close(reiserfs_fs_t *fs, int sync) {
    aal_assert("umka-133", fs != NULL, return);
    aal_assert("umka-134", fs->tree != NULL, return);
    
    if (sync)
	reiserfs_tree_sync(fs);
    
    reiserfs_node_close(fs->tree->root);
    
    aal_free(fs->tree);
    fs->tree = NULL;
}

