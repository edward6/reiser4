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

reiserfs_format_t *reiserfs_format_open(aal_device_t *device, 
    reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-104", device != NULL, return NULL);
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
	
    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_FORMAT_PLUGIN, 
	plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_FORMAT_PLUGIN, 
	    plugin_id, goto error_free_format);
    }
    format->plugin = plugin;
	
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format, open, device)))
    {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't open disk-format %s on %s.", plugin->h.label, 
	    aal_device_name(device));
	goto error_free_format;
    }

    return format;
	
error_free_format:
    aal_free(format);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_format_t *reiserfs_format_create(aal_device_t *device, 
    count_t len, reiserfs_plugin_id_t plugin_id) 
{
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
		
    aal_assert("umka-105", device != NULL, return NULL);

    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_FORMAT_PLUGIN, 
	plugin_id))) 
    {
	libreiser4_factory_find_failed(REISERFS_FORMAT_PLUGIN, plugin_id, 
	    return NULL);
    }
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->plugin = plugin;
	
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format, create, device, len))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create disk-format %s on %s.", plugin->h.label, 
	    aal_device_name(device));
	goto error_free_format;
    }
	
    return format;

error_free_format:
    aal_free(format);
    return NULL;
}

error_t reiserfs_format_sync(reiserfs_format_t *format) {
    aal_assert("umka-107", format != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, 
	format->plugin->format, sync, format->entity);
}

#endif

reiserfs_format_t *reiserfs_format_reopen(reiserfs_format_t *format, 
    aal_device_t *device) 
{
    reiserfs_plugin_t *plugin;

    aal_assert("umka-428", format != NULL, return NULL);

    plugin = format->plugin;
    
    reiserfs_format_close(format);
    return reiserfs_format_open(device, plugin->h.id);
}

void reiserfs_format_close(reiserfs_format_t *format) {
    aal_assert("umka-109", format != NULL, return);
   
    libreiser4_plugin_call(goto error_free_format, 
	format->plugin->format, close, format->entity);
    
error_free_format:    
    aal_free(format);
}

const char *reiserfs_format_format(reiserfs_format_t *format) {
    aal_assert("umka-111", format != NULL, return NULL);
	
    return libreiser4_plugin_call(return NULL, 
	format->plugin->format, format, format->entity);
}

blk_t reiserfs_format_offset(reiserfs_format_t *format) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, 
	format->plugin->format, offset, format->entity);
}

blk_t reiserfs_format_get_root(reiserfs_format_t *format) {
    aal_assert("umka-113", format != NULL, return 0);

    return libreiser4_plugin_call(return 0, format->plugin->format, 
	get_root, format->entity);
}

count_t reiserfs_format_get_blocks(reiserfs_format_t *format) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format, 
	get_blocks, format->entity);
}

count_t reiserfs_format_get_free(reiserfs_format_t *format) {
    aal_assert("umka-426", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format, 
	get_free, format->entity);
}

uint16_t reiserfs_format_get_height(reiserfs_format_t *format) {
    aal_assert("umka-557", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format, 
	get_height, format->entity);
}

#ifndef ENABLE_COMPACT

void reiserfs_format_set_root(reiserfs_format_t *format, blk_t root) {
    aal_assert("umka-420", format != NULL, return);

    libreiser4_plugin_call(return, format->plugin->format, 
	set_root, format->entity, root);
}

void reiserfs_format_set_blocks(reiserfs_format_t *format, count_t blocks) {
    aal_assert("umka-422", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format, 
	set_blocks, format->entity, blocks);
}

void reiserfs_format_set_free(reiserfs_format_t *format, count_t blocks) {
    aal_assert("umka-424", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format, 
	set_free, format->entity, blocks);
}

void reiserfs_format_set_height(reiserfs_format_t *format, uint16_t height) {
    aal_assert("umka-559", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format, 
	set_height, format->entity, height);
}

#endif

reiserfs_plugin_id_t reiserfs_format_journal_plugin_id(reiserfs_format_t *format) {
    aal_assert("umka-115", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format, 
	journal_plugin_id, format->entity);
}

reiserfs_plugin_id_t reiserfs_format_alloc_plugin_id(reiserfs_format_t *format) {
    aal_assert("umka-117", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format, 
	alloc_plugin_id, format->entity);
}

reiserfs_plugin_id_t reiserfs_format_oid_plugin_id(reiserfs_format_t *format) {
    aal_assert("umka-491", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format, 
	oid_plugin_id, format->entity);
}

