/*
    super.c -- format independent super block code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

error_t reiserfs_super_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-103", fs != NULL, return -1);
    aal_assert("umka-104", fs->device != NULL, return -1);
	
    if (fs->super) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Super block already opened.");
	return -1;
    }

    if (!(fs->super = aal_calloc(sizeof(*fs->super), 0)))
	return -1;
	
    if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_FORMAT_PLUGIN, 
	get_mr_format_id(fs->master)))) 
    {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't find disk-format plugin by its identifier %x.", 
	    get_mr_format_id(fs->master));
	goto error_free_super;
    }
    fs->super->plugin = plugin;
	
    reiserfs_plugin_check_routine(plugin->format, open, goto error_free_super);
    if (!(fs->super->entity = plugin->format.open(fs->device))) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't initialize disk-format plugin.");
	goto error_free_super;
    }

    return 0;
	
error_free_super:
    aal_free(fs->super);
    fs->super = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_super_create(reiserfs_fs_t *fs, reiserfs_plugin_id_t plugin_id, 
    count_t len) 
{
    aal_block_t *block;
    reiserfs_plugin_t *plugin;
		
    aal_assert("umka-105", fs != NULL, return -1);

    if (!(plugin = reiserfs_plugins_find_by_coords(REISERFS_FORMAT_PLUGIN, plugin_id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find format plugin by its identifier %x.", plugin_id);
	return -1;
    }
    
    if (!(fs->super = aal_calloc(sizeof(*fs->super), 0)))
	return -1;
	
    /* Creating specified disk-format and format-specific superblock */
    reiserfs_plugin_check_routine(plugin->format, create, goto error_free_super);
    if (!(fs->super->entity = plugin->format.create(fs->device, len, fs->alloc->entity))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create disk-format for %s format.", plugin->h.label);
	goto error_free_super;
    }
    fs->super->plugin = plugin;
	
    return 0;

error_free_super:
    aal_free(fs->super);
    fs->super = NULL;
error:
    return -1;
}

error_t reiserfs_super_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-106", fs != NULL, return -1);
    aal_assert("umka-107", fs->super != NULL, return -1);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, sync, return -1);
    return fs->super->plugin->format.sync(fs->super->entity);
}

#endif

error_t reiserfs_super_reopen(reiserfs_fs_t *fs) {
    aal_assert("umka-427", fs != NULL, return -1);
    aal_assert("umka-428", fs->super != NULL, return -1);

    reiserfs_super_close(fs);
    return reiserfs_super_open(fs);
}

void reiserfs_super_close(reiserfs_fs_t *fs) {
    aal_assert("umka-108", fs != NULL, return);
    aal_assert("umka-109", fs->super != NULL, return);
   
    reiserfs_plugin_check_routine(fs->super->plugin->format, close, goto error_free_super);
    fs->super->plugin->format.close(fs->super->entity);
    
error_free_super:    
    aal_free(fs->super);
    fs->super = NULL;
}

const char *reiserfs_super_format(reiserfs_fs_t *fs) {
    aal_assert("umka-110", fs != NULL, return NULL);
    aal_assert("umka-111", fs->super != NULL, return NULL);
	
    reiserfs_plugin_check_routine(fs->super->plugin->format, format, return NULL);
    return fs->super->plugin->format.format(fs->super->entity);
}

blk_t reiserfs_super_offset(reiserfs_fs_t *fs) {
    aal_assert("umka-359", fs != NULL, return 0);
    aal_assert("umka-360", fs->super != NULL, return 0);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, offset, return 0);
    return fs->super->plugin->format.offset(fs->super->entity);
}

blk_t reiserfs_super_get_root(reiserfs_fs_t *fs) {
    aal_assert("umka-112", fs != NULL, return 0);
    aal_assert("umka-113", fs->super != NULL, return 0);

    reiserfs_plugin_check_routine(fs->super->plugin->format, get_root, return 0);
    return fs->super->plugin->format.get_root(fs->super->entity);
}

count_t reiserfs_super_get_blocks(reiserfs_fs_t *fs) {
    aal_assert("umka-359", fs != NULL, return 0);
    aal_assert("umka-360", fs->super != NULL, return 0);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, get_blocks, return 0);
    return fs->super->plugin->format.get_blocks(fs->super->entity);
}

void reiserfs_super_set_root(reiserfs_fs_t *fs, blk_t root) {
    aal_assert("umka-419", fs != NULL, return);
    aal_assert("umka-420", fs->super != NULL, return);

    reiserfs_plugin_check_routine(fs->super->plugin->format, set_root, return);
    fs->super->plugin->format.set_root(fs->super->entity, root);
}

void reiserfs_super_set_blocks(reiserfs_fs_t *fs, count_t blocks) {
    aal_assert("umka-421", fs != NULL, return);
    aal_assert("umka-422", fs->super != NULL, return);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, set_blocks, return);
    fs->super->plugin->format.set_blocks(fs->super->entity, blocks);
}

void reiserfs_super_set_free(reiserfs_fs_t *fs, count_t blocks) {
    aal_assert("umka-423", fs != NULL, return);
    aal_assert("umka-424", fs->super != NULL, return);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, set_free, return);
    fs->super->plugin->format.set_free(fs->super->entity, blocks);
}

count_t reiserfs_super_get_free(reiserfs_fs_t *fs) {
    aal_assert("umka-425", fs != NULL, return 0);
    aal_assert("umka-426", fs->super != NULL, return 0);
    
    reiserfs_plugin_check_routine(fs->super->plugin->format, get_free, return 0);
    return fs->super->plugin->format.get_free(fs->super->entity);
}

reiserfs_plugin_id_t reiserfs_super_journal_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-114", fs != NULL, return -1);
    aal_assert("umka-115", fs->super != NULL, return -1);
	
    reiserfs_plugin_check_routine(fs->super->plugin->format, 
	journal_plugin_id, return -1);
    return fs->super->plugin->format.journal_plugin_id(fs->super->entity);
}

reiserfs_plugin_id_t reiserfs_super_alloc_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-116", fs != NULL, return -1);
    aal_assert("umka-117", fs->super != NULL, return -1);
	
    reiserfs_plugin_check_routine(fs->super->plugin->format, 
	alloc_plugin_id, return -1);
    return fs->super->plugin->format.alloc_plugin_id(fs->super->entity);
}

reiserfs_plugin_id_t reiserfs_super_node_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-118", fs != NULL, return -1);
    aal_assert("umka-119", fs->super != NULL, return -1);
	
    reiserfs_plugin_check_routine(fs->super->plugin->format, 
	node_plugin_id, return -1);
    return fs->super->plugin->format.node_plugin_id(fs->super->entity);
}

