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
    reiserfs_id_t pid) 
{
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-104", device != NULL, return NULL);
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
	
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_FORMAT_PLUGIN, pid))) 
	libreiser4_factory_failed(goto error_free_format, find, format, pid);
    
    format->plugin = plugin;
	
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format_ops, open, device)))
    {
	aal_throw_fatal(EO_OK, "Can't open disk-format %s on %s.\n", plugin->h.label, 
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
    count_t len, uint16_t tail_policy, reiserfs_id_t pid) 
{
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
		
    aal_assert("umka-105", device != NULL, return NULL);

    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_FORMAT_PLUGIN, pid))) 
	libreiser4_factory_failed(return NULL, find, format, pid); 
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->plugin = plugin;
	
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format_ops, create, device, len, tail_policy))) 
    {
	aal_throw_error(EO_OK, "Can't create disk-format %s on %s.\n", plugin->h.label, 
	    aal_device_name(device));
	goto error_free_format;
    }
	
    return format;

error_free_format:
    aal_free(format);
    return NULL;
}

errno_t reiserfs_format_sync(reiserfs_format_t *format) {
    aal_assert("umka-107", format != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, 
	format->plugin->format_ops, sync, format->entity);
}

errno_t reiserfs_format_check(reiserfs_format_t *format, int flags) {
    aal_assert("umka-829", format != NULL, return -1);

    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	check, format->entity, flags);
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
	format->plugin->format_ops, close, format->entity);
    
error_free_format:    
    aal_free(format);
}

int reiserfs_format_confirm(reiserfs_format_t *format) {
    aal_assert("umka-832", format != NULL, return 0);

    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	confirm, format->entity);
}

const char *reiserfs_format_format(reiserfs_format_t *format) {
    aal_assert("umka-111", format != NULL, return NULL);
	
    return libreiser4_plugin_call(return NULL, 
	format->plugin->format_ops, format, format->entity);
}

blk_t reiserfs_format_offset(reiserfs_format_t *format) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, 
	format->plugin->format_ops, offset, format->entity);
}

blk_t reiserfs_format_get_root(reiserfs_format_t *format) {
    aal_assert("umka-113", format != NULL, return 0);

    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_root, format->entity);
}

count_t reiserfs_format_get_blocks(reiserfs_format_t *format) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_blocks, format->entity);
}

count_t reiserfs_format_get_free(reiserfs_format_t *format) {
    aal_assert("umka-426", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_free, format->entity);
}

uint16_t reiserfs_format_get_height(reiserfs_format_t *format) {
    aal_assert("umka-557", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_height, format->entity);
}

#ifndef ENABLE_COMPACT

void reiserfs_format_set_root(reiserfs_format_t *format, blk_t root) {
    aal_assert("umka-420", format != NULL, return);

    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_root, format->entity, root);
}

void reiserfs_format_set_blocks(reiserfs_format_t *format, count_t blocks) {
    aal_assert("umka-422", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_blocks, format->entity, blocks);
}

void reiserfs_format_set_free(reiserfs_format_t *format, count_t blocks) {
    aal_assert("umka-424", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_free, format->entity, blocks);
}

void reiserfs_format_set_height(reiserfs_format_t *format, uint16_t height) {
    aal_assert("umka-559", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_height, format->entity, height);
}

#endif

reiserfs_id_t reiserfs_format_journal_pid(reiserfs_format_t *format) {
    aal_assert("umka-115", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	journal_pid, format->entity);
}

reiserfs_id_t reiserfs_format_alloc_pid(reiserfs_format_t *format) {
    aal_assert("umka-117", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	alloc_pid, format->entity);
}

reiserfs_id_t reiserfs_format_oid_pid(reiserfs_format_t *format) {
    aal_assert("umka-491", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	oid_pid, format->entity);
}

