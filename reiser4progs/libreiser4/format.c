/*
    format.c -- disk format common code. This code is wrapper for disk-format
    plugin. It is used by filesystem code (filesystem.c) for working with 
    different disk-format plugins in independent maner.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

/* 
    Opens disk-format on specified device. Actually it just calls specified by
    "pid" disk-format plugin and that plugin makes all dirty work.
*/
reiser4_format_t *reiser4_format_open(
    aal_device_t *device,	/* device disk-format instance will be opened on */
    rpid_t pid		/* disk-format plugin id to be used */
) {
    reiser4_format_t *format;
    reiser4_plugin_t *plugin;
	
    aal_assert("umka-104", device != NULL, return NULL);
    
    /* Allocating memory for instance of disk-format */
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;
    
    /* Finding needed disk-format plugin by its plugin id */
    if (!(plugin = libreiser4_factory_ifind(FORMAT_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find disk-format plugin by its id 0x%x.", pid);
	goto error_free_format;
    }
    
    /* Initializing disk-format entity by calling plugin */
    if (!(format->entity = plugin_call(goto error_free_format, 
	plugin->format_ops, open, device)))
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

/* Creates disk-format structures on specified device */
reiser4_format_t *reiser4_format_create(
    aal_device_t *device,	/* device disk-format will be created on */
    count_t len,		/* filesystem length in blocks */
    uint16_t tail,		/* tail policy to be used */
    rpid_t pid			/* disk-format plugin id to be used */
) {
    reiser4_format_t *format;
    reiser4_plugin_t *plugin;
		
    aal_assert("umka-105", device != NULL, return NULL);

    /* Getting needed plugin from plugin factory */
    if (!(plugin = libreiser4_factory_ifind(FORMAT_PLUGIN_TYPE, pid)))  {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find disk-format plugin by its id 0x%x.", pid);
	return NULL;
    }
    
    /* Allocating memory */
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    format->device = device;
	
    /* 
	Initializing entity of disk-format by means of calling "create" method 
	from found plugin. Plugin "create" method will be creating all disk
	structures, namely, format-specific super block.
    */
    if (!(format->entity = plugin_call(goto error_free_format, 
	plugin->format_ops, create, device, len, tail))) 
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

/* Saves passed format on its device */
errno_t reiser4_format_sync(
    reiser4_format_t *format	/* disk-format to be saved */
) {
    aal_assert("umka-107", format != NULL, return -1);
    
    return plugin_call(return -1, 
	format->entity->plugin->format_ops, sync, format->entity);
}

/* Checks passed disk-format for validness */
errno_t reiser4_format_valid(
    reiser4_format_t *format	/* format to be checked */
) {
    aal_assert("umka-829", format != NULL, return -1);

    return plugin_call(return -1, format->entity->plugin->format_ops, 
	valid, format->entity);
}

/* This function is used as callback for marking format blocks used */
errno_t callback_action_mark(
    aal_device_t *device,	/* device for operating on */ 
    blk_t blk,			/* block number to be marked */
    void *data			/* pointer to block allocator */
) {
    return reiser4_alloc_mark((reiser4_alloc_t *)data, blk);
}

/* Marks format area as used */
errno_t reiser4_format_mark(
    reiser4_format_t *format,	/* format function works with */
    reiser4_alloc_t *alloc	/* block allocator */
) {
    if (plugin_call(return -1, format->entity->plugin->format_ops,
	    skipped_layout, format->entity, callback_action_mark, alloc))
	return -1;
    
    if (plugin_call(return -1, format->entity->plugin->format_ops,
	    format_layout, format->entity, callback_action_mark, alloc))
	return -1;
    
    /* 
	FIXME-UMKA: Here we should check if we need to mark journal. Journal may
	be relocated (reiser3).
    */
    if (plugin_call(return -1, format->entity->plugin->format_ops,
	    journal_layout, format->entity, callback_action_mark, alloc))
	return -1;
    
    if (plugin_call(return -1, format->entity->plugin->format_ops,
	    alloc_layout, format->entity, callback_action_mark, alloc))
	return -1;
    
    return 0;
}

#endif

/* Reopens disk-format on specified device */
reiser4_format_t *reiser4_format_reopen(
    reiser4_format_t *format,	/* format to be reopened */
    aal_device_t *device	/* device format will be reopened on */
) {
    aal_assert("umka-428", format != NULL, return NULL);

    reiser4_format_close(format);
    return reiser4_format_open(device, format->entity->plugin->h.id);
}

/* Closes passed disk-format */
void reiser4_format_close(
    reiser4_format_t *format	/* format to be closed */
) {
    aal_assert("umka-109", format != NULL, return);
   
    plugin_call(goto error_free_format, 
	format->entity->plugin->format_ops, close, format->entity);
    
error_free_format:    
    aal_free(format);
}

/* Confirms disk-format (simple check) */
int reiser4_format_confirm(
    reiser4_format_t *format	/* format to be checked */
) {
    aal_assert("umka-832", format != NULL, return 0);

    return plugin_call(return 0, format->entity->plugin->format_ops, 
	confirm, format->device);
}

/* Returns string described used disk-format */
const char *reiser4_format_name(
    reiser4_format_t *format	/* disk-format to be inspected */
) {
    aal_assert("umka-111", format != NULL, return NULL);
	
    return plugin_call(return NULL, 
	format->entity->plugin->format_ops, name, format->entity);
}

/* Returns root block from passed disk-format */
blk_t reiser4_format_get_root(
    reiser4_format_t *format	/* format to be used */
) {
    aal_assert("umka-113", format != NULL, return 0);

    return plugin_call(return 0, format->entity->plugin->format_ops, 
	get_root, format->entity);
}

/* Returns filesystem length in blocks from passed disk-format */
count_t reiser4_format_get_len(
    reiser4_format_t *format	/* disk-format to be inspected */
) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return plugin_call(return 0, format->entity->plugin->format_ops, 
	get_len, format->entity);
}

/* Returns number of free blocks */
count_t reiser4_format_get_free(
    reiser4_format_t *format	/* format to be used */
) {
    aal_assert("umka-426", format != NULL, return 0);
    
    return plugin_call(return 0, format->entity->plugin->format_ops, 
	get_free, format->entity);
}

/* Returns tree height */
uint16_t reiser4_format_get_height(
    reiser4_format_t *format	/* format to be inspected */
) {
    aal_assert("umka-557", format != NULL, return 0);
    
    return plugin_call(return 0, format->entity->plugin->format_ops, 
	get_height, format->entity);
}

#ifndef ENABLE_COMPACT

/* Sets new root block */
void reiser4_format_set_root(
    reiser4_format_t *format,	/* format new root blocks will be set in */
    blk_t root			/* new root block */
) {
    aal_assert("umka-420", format != NULL, return);

    plugin_call(return, format->entity->plugin->format_ops, 
	set_root, format->entity, root);
}

/* Sets new filesystem length */
void reiser4_format_set_len(
    reiser4_format_t *format,	/* format instance to be used */
    count_t blocks		/* new length in blocks */
) {
    aal_assert("umka-422", format != NULL, return);
    
    plugin_call(return, format->entity->plugin->format_ops, 
	set_len, format->entity, blocks);
}

/* Sets free block count */
void reiser4_format_set_free(
    reiser4_format_t *format,	/* format to be used */
    count_t blocks		/* new free block count */
) {
    aal_assert("umka-424", format != NULL, return);
    
    plugin_call(return, format->entity->plugin->format_ops, 
	set_free, format->entity, blocks);
}

/* Sets new tree height */
void reiser4_format_set_height(
    reiser4_format_t *format,	/* format to be used */
    uint8_t height		/* new tree height */
) {
    aal_assert("umka-559", format != NULL, return);
    
    plugin_call(return, format->entity->plugin->format_ops, 
	set_height, format->entity, height);
}

#endif

/* Returns jouranl plugin id in use */
rpid_t reiser4_format_journal_pid(
    reiser4_format_t *format	/* disk-format journal pid will be obtained from */
) {
    aal_assert("umka-115", format != NULL, return -1);
	
    return plugin_call(return -1, format->entity->plugin->format_ops, 
	journal_pid, format->entity);
}

/* Returns block allocator plugin id in use */
rpid_t reiser4_format_alloc_pid(
    reiser4_format_t *format	/* disk-format allocator pid will be obtained from */
) {
    aal_assert("umka-117", format != NULL, return -1);
	
    return plugin_call(return -1, format->entity->plugin->format_ops, 
	alloc_pid, format->entity);
}

/* Returns oid allocator plugin id in use */
rpid_t reiser4_format_oid_pid(
    reiser4_format_t *format	/* disk-format oid allocator pid will be obtained from */
) {
    aal_assert("umka-491", format != NULL, return -1);
	
    return plugin_call(return -1, format->entity->plugin->format_ops, 
	oid_pid, format->entity);
}

errno_t reiser4_format_layout(reiser4_format_t *format, 
    reiser4_action_func_t action_func, void *data)
{
    if (reiser4_format_skipped_layout(format, action_func, data))
	return -1;
    
    if (reiser4_format_format_layout(format, action_func, data))
	return -1;
    
    if (reiser4_format_journal_layout(format, action_func, data))
	return -1;
    
    return reiser4_format_alloc_layout(format, action_func, data);
}

errno_t reiser4_format_skipped_layout(reiser4_format_t *format, 
    reiser4_action_func_t action_func, void *data)
{
    aal_assert("umka-1083", format != NULL, return -1);
    aal_assert("umka-1084", action_func != NULL, return -1);

    return plugin_call(return -1, format->entity->plugin->format_ops,
	skipped_layout, format->entity, action_func, data);
}

errno_t reiser4_format_format_layout(reiser4_format_t *format, 
    reiser4_action_func_t action_func, void *data)
{
    aal_assert("umka-1076", format != NULL, return -1);
    aal_assert("umka-1077", action_func != NULL, return -1);

    return plugin_call(return -1, format->entity->plugin->format_ops,
	format_layout, format->entity, action_func, data);
}

errno_t reiser4_format_journal_layout(reiser4_format_t *format, 
    reiser4_action_func_t action_func, void *data)
{
    aal_assert("umka-1078", format != NULL, return -1);
    aal_assert("umka-1079", action_func != NULL, return -1);

    return plugin_call(return -1, format->entity->plugin->format_ops,
	journal_layout, format->entity, action_func, data);
}

errno_t reiser4_format_alloc_layout(reiser4_format_t *format, 
    reiser4_action_func_t action_func, void *data)
{
    aal_assert("umka-1080", format != NULL, return -1);
    aal_assert("umka-1081", action_func != NULL, return -1);

    return plugin_call(return -1, format->entity->plugin->format_ops,
	alloc_layout, format->entity, action_func, data);
}

