/*
    format.c -- format independent code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

error_t reiserfs_format_open(reiserfs_fs_t *fs) {
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-103", fs != NULL, return -1);
    aal_assert("umka-104", fs->host_device != NULL, return -1);
	
    if (fs->format) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Format already opened.");
	return -1;
    }

    if (!(fs->format = aal_calloc(sizeof(*fs->format), 0)))
	return -1;
	
    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_FORMAT_PLUGIN, 
	get_mr_format_id(fs->master)))) 
    {
	libreiser4_factory_find_failed(REISERFS_FORMAT_PLUGIN, 
	    get_mr_format_id(fs->master), goto error_free_format);
    }
    fs->format->plugin = plugin;
	
    if (!(fs->format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format, open, fs->host_device, fs->journal_device)))
    {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't open disk-format plugin.");
	goto error_free_format;
    }

    return 0;
	
error_free_format:
    aal_free(fs->format);
    fs->format = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_format_create(reiserfs_fs_t *fs, 
    reiserfs_plugin_id_t format_plugin_id, count_t len, 
    reiserfs_params_opaque_t *journal_params) 
{
    aal_block_t *block;
    reiserfs_plugin_t *plugin;
		
    aal_assert("umka-105", fs != NULL, return -1);

    if (fs->format) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Format already opened.");
	return -1;
    }
    
    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_FORMAT_PLUGIN, 
	format_plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_FORMAT_PLUGIN, format_plugin_id, 
	    return -1);
    }
    
    if (!(fs->format = aal_calloc(sizeof(*fs->format), 0)))
	return -1;
	
    if (!(fs->format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format, create, fs->host_device, len, fs->journal_device, 
	journal_params))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create disk-format for %s format.", plugin->h.label);
	goto error_free_format;
    }
    fs->format->plugin = plugin;
	
    return 0;

error_free_format:
    aal_free(fs->format);
    fs->format = NULL;
error:
    return -1;
}

error_t reiserfs_format_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-106", fs != NULL, return -1);
    aal_assert("umka-107", fs->format != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, 
	fs->format->plugin->format, sync, fs->format->entity);
}

#endif

error_t reiserfs_format_reopen(reiserfs_fs_t *fs) {
    aal_assert("umka-427", fs != NULL, return -1);
    aal_assert("umka-428", fs->format != NULL, return -1);

    reiserfs_format_close(fs);
    return reiserfs_format_open(fs);
}

void reiserfs_format_close(reiserfs_fs_t *fs) {
    aal_assert("umka-108", fs != NULL, return);
    aal_assert("umka-109", fs->format != NULL, return);
   
    libreiser4_plugin_call(goto error_free_format, 
	fs->format->plugin->format, close, fs->format->entity);
    
error_free_format:    
    aal_free(fs->format);
    fs->format = NULL;
}

const char *reiserfs_format_format(reiserfs_fs_t *fs) {
    aal_assert("umka-110", fs != NULL, return NULL);
    aal_assert("umka-111", fs->format != NULL, return NULL);
	
    return libreiser4_plugin_call(return NULL, 
	fs->format->plugin->format, format, fs->format->entity);
}

blk_t reiserfs_format_offset(reiserfs_fs_t *fs) {
    aal_assert("umka-359", fs != NULL, return 0);
    aal_assert("umka-360", fs->format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, 
	fs->format->plugin->format, offset, fs->format->entity);
}

blk_t reiserfs_format_get_root(reiserfs_fs_t *fs) {
    aal_assert("umka-112", fs != NULL, return 0);
    aal_assert("umka-113", fs->format != NULL, return 0);

    return libreiser4_plugin_call(return 0, fs->format->plugin->format, 
	get_root, fs->format->entity);
}

count_t reiserfs_format_get_blocks(reiserfs_fs_t *fs) {
    aal_assert("umka-359", fs != NULL, return 0);
    aal_assert("umka-360", fs->format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->format->plugin->format, 
	get_blocks, fs->format->entity);
}

count_t reiserfs_format_get_free(reiserfs_fs_t *fs) {
    aal_assert("umka-425", fs != NULL, return 0);
    aal_assert("umka-426", fs->format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->format->plugin->format, 
	get_free, fs->format->entity);
}

uint16_t reiserfs_format_get_height(reiserfs_fs_t *fs) {
    aal_assert("umka-556", fs != NULL, return 0);
    aal_assert("umka-557", fs->format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, fs->format->plugin->format, 
	get_height, fs->format->entity);
}

#ifndef ENABLE_COMPACT

void reiserfs_format_set_root(reiserfs_fs_t *fs, blk_t root) {
    aal_assert("umka-419", fs != NULL, return);
    aal_assert("umka-420", fs->format != NULL, return);

    libreiser4_plugin_call(return, fs->format->plugin->format, 
	set_root, fs->format->entity, root);
}

void reiserfs_format_set_blocks(reiserfs_fs_t *fs, count_t blocks) {
    aal_assert("umka-421", fs != NULL, return);
    aal_assert("umka-422", fs->format != NULL, return);
    
    libreiser4_plugin_call(return, fs->format->plugin->format, 
	set_blocks, fs->format->entity, blocks);
}

void reiserfs_format_set_free(reiserfs_fs_t *fs, count_t blocks) {
    aal_assert("umka-423", fs != NULL, return);
    aal_assert("umka-424", fs->format != NULL, return);
    
    libreiser4_plugin_call(return, fs->format->plugin->format, 
	set_free, fs->format->entity, blocks);
}

void reiserfs_format_set_height(reiserfs_fs_t *fs, uint16_t height) {
    aal_assert("umka-558", fs != NULL, return);
    aal_assert("umka-559", fs->format != NULL, return);
    
    libreiser4_plugin_call(return, fs->format->plugin->format, 
	set_height, fs->format->entity, height);
}

#endif

reiserfs_plugin_id_t reiserfs_format_journal_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-114", fs != NULL, return -1);
    aal_assert("umka-115", fs->format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, fs->format->plugin->format, 
	journal_plugin_id, fs->format->entity);
}

reiserfs_opaque_t *reiserfs_format_journal(reiserfs_fs_t *fs) {
    aal_assert("umka-492", fs != NULL, return NULL);
    aal_assert("umka-493", fs->format != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, fs->format->plugin->format, 
	journal, fs->format->entity);
}

reiserfs_plugin_id_t reiserfs_format_alloc_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-116", fs != NULL, return -1);
    aal_assert("umka-117", fs->format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, fs->format->plugin->format, 
	alloc_plugin_id, fs->format->entity);
}

reiserfs_opaque_t *reiserfs_format_alloc(reiserfs_fs_t *fs) {
    aal_assert("umka-494", fs != NULL, return NULL);
    aal_assert("umka-495", fs->format != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, fs->format->plugin->format, 
	alloc, fs->format->entity);
}

reiserfs_plugin_id_t reiserfs_format_oid_plugin_id(reiserfs_fs_t *fs) {
    aal_assert("umka-490", fs != NULL, return -1);
    aal_assert("umka-491", fs->format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, fs->format->plugin->format, 
	oid_plugin_id, fs->format->entity);
}

reiserfs_opaque_t *reiserfs_format_oid(reiserfs_fs_t *fs) {
    aal_assert("umka-496", fs != NULL, return NULL);
    aal_assert("umka-497", fs->format != NULL, return NULL);
    
    return libreiser4_plugin_call(return NULL, fs->format->plugin->format, 
	oid, fs->format->entity);
}

