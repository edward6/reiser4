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
reiserfs_format_t *reiserfs_format_open(
    aal_device_t *device,	/* device disk-format instance will be opened on */
    reiserfs_id_t pid		/* disk-format plugin id to be used */
) {
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-104", device != NULL, return NULL);
    
    /* Allocating memory for instance of disk-format */
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    /* Finding needed plugin by its plugin id */
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_FORMAT_PLUGIN, pid))) 
	libreiser4_factory_failed(goto error_free_format, find, format, pid);
    
    format->plugin = plugin;
    
    /* Initializing disk-format entity by calling plugin */
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
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
reiserfs_format_t *reiserfs_format_create(
    aal_device_t *device,	/* device disk-format will be created on */
    count_t len,		/* filesystem length in blocks */
    uint16_t drop_policy,	/* drop policy to be used */
    reiserfs_id_t pid		/* disk-format plugin id to be used */
) {
    reiserfs_format_t *format;
    reiserfs_plugin_t *plugin;
		
    aal_assert("umka-105", device != NULL, return NULL);

    /* Getting needed plugin from plugin factory */
    if (!(plugin = libreiser4_factory_find_by_id(REISERFS_FORMAT_PLUGIN, pid))) 
	libreiser4_factory_failed(return NULL, find, format, pid); 
    
    /* Allocating memory */
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->plugin = plugin;
	
    /* 
	Initializing entity of disk-format by means of calling "create" method 
	from found plugin. Plugin "create" method will be creating all disk
	structures, namely, format-specific super block.
    */
    if (!(format->entity = libreiser4_plugin_call(goto error_free_format, 
	plugin->format_ops, create, device, len, drop_policy))) 
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
errno_t reiserfs_format_sync(
    reiserfs_format_t *format	/* disk-format to be saved */
) {
    aal_assert("umka-107", format != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, 
	format->plugin->format_ops, sync, format->entity);
}

/* Checks passed disk-format for validness */
errno_t reiserfs_format_check(
    reiserfs_format_t *format,	/* format to be checked */
    int flags			/* some flags (not used at the moment) */
) {
    aal_assert("umka-829", format != NULL, return -1);

    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	check, format->entity, flags);
}

#endif

/* Reopens disk-format on specified device */
reiserfs_format_t *reiserfs_format_reopen(
    reiserfs_format_t *format,	/* format to be reopened */
    aal_device_t *device	/* device format will be reopened on */
) {
    reiserfs_plugin_t *plugin;

    aal_assert("umka-428", format != NULL, return NULL);

    plugin = format->plugin;
    
    reiserfs_format_close(format);
    return reiserfs_format_open(device, plugin->h.id);
}

/* Closes passed disk-format */
void reiserfs_format_close(
    reiserfs_format_t *format	/* format to be closed */
) {
    aal_assert("umka-109", format != NULL, return);
   
    libreiser4_plugin_call(goto error_free_format, 
	format->plugin->format_ops, close, format->entity);
    
error_free_format:    
    aal_free(format);
}

/* Confirms disk-format (simple check) */
int reiserfs_format_confirm(
    reiserfs_format_t *format	/* format to be checked */
) {
    aal_assert("umka-832", format != NULL, return 0);

    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	confirm, format->entity);
}

/* Returns string described used disk-format */
const char *reiserfs_format_format(
    reiserfs_format_t *format	/* disk-format to be inspected */
) {
    aal_assert("umka-111", format != NULL, return NULL);
	
    return libreiser4_plugin_call(return NULL, 
	format->plugin->format_ops, format, format->entity);
}

/* Retutns position in blocks where format lies */
blk_t reiserfs_format_offset(
    reiserfs_format_t *format	/* disk-format to be used */
) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, 
	format->plugin->format_ops, offset, format->entity);
}

/* Returns root block from passed disk-format */
blk_t reiserfs_format_get_root(
    reiserfs_format_t *format	/* format to be used */
) {
    aal_assert("umka-113", format != NULL, return 0);

    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_root, format->entity);
}

/* Returns filesystem length in blocks from passed disk-format */
count_t reiserfs_format_get_blocks(
    reiserfs_format_t *format	/* disk-format to be inspected */
) {
    aal_assert("umka-360", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_blocks, format->entity);
}

/* Returns number of free blocks */
count_t reiserfs_format_get_free(
    reiserfs_format_t *format	/* format to be used */
) {
    aal_assert("umka-426", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_free, format->entity);
}

/* Returns tree height */
uint16_t reiserfs_format_get_height(
    reiserfs_format_t *format	/* format to be inspected */
) {
    aal_assert("umka-557", format != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, format->plugin->format_ops, 
	get_height, format->entity);
}

#ifndef ENABLE_COMPACT

/* Sets new root block */
void reiserfs_format_set_root(
    reiserfs_format_t *format,	/* format new root blocks will be set in */
    blk_t root			/* new root block */
) {
    aal_assert("umka-420", format != NULL, return);

    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_root, format->entity, root);
}

/* Sets new filesystem length */
void reiserfs_format_set_blocks(
    reiserfs_format_t *format,	/* format instance to be used */
    count_t blocks		/* new length in blocks */
) {
    aal_assert("umka-422", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_blocks, format->entity, blocks);
}

/* Sets free block count */
void reiserfs_format_set_free(
    reiserfs_format_t *format,	/* format to be used */
    count_t blocks		/* new free block count */
) {
    aal_assert("umka-424", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_free, format->entity, blocks);
}

/* Sets new tree height */
void reiserfs_format_set_height(
    reiserfs_format_t *format,	/* format to be used */
    uint16_t height		/* new tree height */
) {
    aal_assert("umka-559", format != NULL, return);
    
    libreiser4_plugin_call(return, format->plugin->format_ops, 
	set_height, format->entity, height);
}

#endif

/* Returns jouranl plugin id in use */
reiserfs_id_t reiserfs_format_journal_pid(
    reiserfs_format_t *format	/* disk-format journal pid will be obtained from */
) {
    aal_assert("umka-115", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	journal_pid, format->entity);
}

/* Returns block allocator plugin id in use */
reiserfs_id_t reiserfs_format_alloc_pid(
    reiserfs_format_t *format	/* disk-format allocator pid will be obtained from */
) {
    aal_assert("umka-117", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	alloc_pid, format->entity);
}

/* Returns oid allocator plugin id in use */
reiserfs_id_t reiserfs_format_oid_pid(
    reiserfs_format_t *format	/* disk-format oid allocator pid will be obtained from */
) {
    aal_assert("umka-491", format != NULL, return -1);
	
    return libreiser4_plugin_call(return -1, format->plugin->format_ops, 
	oid_pid, format->entity);
}

