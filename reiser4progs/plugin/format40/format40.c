/*
    format40.c -- default disk-layout plugin for reiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format40.h"

#define format40_super(block) ((format40_super_t *)block->data)

static reiser4_core_t *core = NULL;

static errno_t format40_super_check(format40_super_t *super, 
    aal_device_t *device) 
{
    blk_t offset;
    blk_t dev_len = aal_device_len(device);
    
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %llu for device "
	    "length %llu blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }
    
    offset = (FORMAT40_OFFSET / aal_device_get_bs(device));
    if (get_sb_root_block(super) < offset || get_sb_root_block(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Superblock has an invalid root block %llu for device "
	    "length %llu blocks.", get_sb_root_block(super), dev_len);
	return -1;
    }
    return 0;
}

static int format40_signature(format40_super_t *super) {
    return aal_strncmp(super->sb_magic, FORMAT40_MAGIC, 
	aal_strlen(FORMAT40_MAGIC)) == 0;
}

static aal_block_t *format40_super_open(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    
    offset = (FORMAT40_OFFSET / aal_device_get_bs(device));
	
    if (!(block = aal_block_read(device, offset))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't read block %llu. %s.", offset, 
	   aal_device_error(device));
	return NULL;
    }

    if (!format40_signature((format40_super_t *)block->data))
	return NULL;
    
    return block;
}

static reiser4_entity_t *format40_open(aal_device_t *device) {
    format40_t *format;

    aal_assert("umka-393", device != NULL, return NULL);

    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    format->device = device;
    
    if (!(format->block = format40_super_open(device)))
	goto error_free_format;
		
    return (reiser4_entity_t *)format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

/* This function should create super block and update all copies */
static reiser4_entity_t *format40_create(aal_device_t *device, 
    count_t blocks, uint16_t drop_policy)
{
    blk_t blk;
    format40_t *format;
    format40_super_t *super;
    
    aal_assert("umka-395", device != NULL, return NULL);
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;

    if (!(format->block = aal_block_alloc(device, (FORMAT40_OFFSET / 
	aal_device_get_bs(device)), 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    
    super = (format40_super_t *)format->block->data;
    aal_memcpy(super->sb_magic, FORMAT40_MAGIC, 
	aal_strlen(FORMAT40_MAGIC));

    set_sb_block_count(super, blocks);
    set_sb_tree_height(super, 2);
    set_sb_flushes(super, 0);
    set_sb_drop_policy(super, drop_policy);

    return (reiser4_entity_t *)format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

/* This function should update all copies of the super block */
static errno_t format40_sync(reiser4_entity_t *entity) {
    format40_t *format;
    
    aal_assert("umka-394", entity != NULL, return -1); 
   
    format = (format40_t *)entity;
    
    if (aal_block_write(format->block)) {
	blk_t offset = aal_block_get_nr(format->block);
	
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %llu. %s.", offset, 
	    aal_device_error(format->device));
	
	return -1;
    }
    
    return 0;
}

#endif

static errno_t format40_valid(reiser4_entity_t *entity, 
    int flags) 
{
    format40_t *format;
    
    aal_assert("umka-397", entity != NULL, return -1);
    
    format = (format40_t *)entity;
    
    return format40_super_check(format40_super(format->block), 
	format->device);
}

static void format40_close(reiser4_entity_t *entity) {
    aal_assert("umka-398", entity != NULL, return);
    
    aal_block_free(((format40_t *)entity)->block);
    aal_free(entity);
}

static int format40_confirm(aal_device_t *device) {
    aal_block_t *block;

    aal_assert("umka-733", device != NULL, return 0);
    
    if (!(block = format40_super_open(device)))
	return 0;
	
    aal_block_free(block);
    return 1;
}

static void format40_oid_area(reiser4_entity_t *entity, 
    void **oid_start, uint32_t *oid_len) 
{
    format40_super_t *super;
    
    aal_assert("umka-732", entity != NULL, return);
    
    super = format40_super(((format40_t *)entity)->block);
    
    *oid_start = &super->sb_oid;
    *oid_len = &super->sb_file_count - &super->sb_oid;
}

static void format40_journal_area(reiser4_entity_t *entity, 
    blk_t *start, blk_t *end) 
{
    aal_assert("umka-734", entity != NULL, return);
    aal_assert("umka-978", start != NULL, return);
    aal_assert("umka-979", end != NULL, return);
    
    *start = (FORMAT40_JHEADER / 
	aal_device_get_bs(((format40_t *)entity)->device));
    
    *end = (FORMAT40_JFOOTER / 
	aal_device_get_bs(((format40_t *)entity)->device));
}

static const char *formats[] = {"4.0"};

static const char *format40_name(reiser4_entity_t *entity) {
    return formats[0];
}

static reiser4_id_t format40_journal_pid(reiser4_entity_t *entity) {
    return JOURNAL_REISER40_ID;
}

static reiser4_id_t format40_alloc_pid(reiser4_entity_t *entity) {
    return ALLOC_REISER40_ID;
}

static reiser4_id_t format40_oid_pid(reiser4_entity_t *entity) {
    return OID_REISER40_ID;
}

static blk_t format40_offset(reiser4_entity_t *entity) {
    aal_assert("umka-399", entity != NULL, return 0);

    return (FORMAT40_OFFSET / 
	aal_device_get_bs(((format40_t *)entity)->device));
}

static blk_t format40_get_root(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-400", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_root_block(super);
}

static count_t format40_get_len(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-401", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_block_count(super);
}

static count_t format40_get_free(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-402", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_free_blocks(super);
}

static uint16_t format40_get_height(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-555", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_tree_height(super);
}

#ifndef ENABLE_COMPACT

static void format40_set_root(reiser4_entity_t *entity, 
    blk_t root) 
{
    format40_super_t *super;
    
    aal_assert("umka-403", entity != NULL, return);
    
    super = format40_super(((format40_t *)entity)->block);
    set_sb_root_block(super, root);
}

static void format40_set_len(reiser4_entity_t *entity, 
    count_t blocks) 
{
    format40_super_t *super;
    
    aal_assert("umka-404", entity != NULL, return);
    
    super = format40_super(((format40_t *)entity)->block);
    set_sb_block_count(super, blocks);
}

static void format40_set_free(reiser4_entity_t *entity, 
    count_t blocks) 
{
    format40_super_t *super;
    
    aal_assert("umka-405", entity != NULL, return);
    
    super = format40_super(((format40_t *)entity)->block);
    set_sb_free_blocks(super, blocks);
}

static void format40_set_height(reiser4_entity_t *entity, 
    uint16_t height) 
{
    format40_super_t *super;
    
    aal_assert("umka-555", entity != NULL, return);

    super = format40_super(((format40_t *)entity)->block);
    set_sb_tree_height(super, height);
}

#endif

static reiser4_plugin_t format40_plugin = {
    .format_ops = {
	.h = {
	    .handle = NULL,
	    .id = FORMAT_REISER40_ID,
	    .type = FORMAT_PLUGIN_TYPE,
	    .label = "format40",
	    .desc = "Disk-format for reiserfs 4.0, ver. " VERSION,
	},
	.open		= format40_open,
	.valid		= format40_valid,
#ifndef ENABLE_COMPACT	
	.sync		= format40_sync,
	.create		= format40_create,
#else
	.sync		= NULL,
	.create		= NULL,
#endif
	.oid_area	= format40_oid_area,
	.journal_area	= format40_journal_area,
	
	.close		= format40_close,
	.confirm	= format40_confirm,
	.name		= format40_name,
	.offset		= format40_offset,
	.get_root	= format40_get_root,
	.get_len	= format40_get_len,
	.get_free	= format40_get_free,
	.get_height	= format40_get_height,
	
#ifndef ENABLE_COMPACT	
	.set_root	= format40_set_root,
	.set_len	= format40_set_len,
	.set_free	= format40_set_free,
	.set_height	= format40_set_height,
#else
	.set_root	= NULL,
	.set_len	= NULL,
	.set_free	= NULL,
	.set_height	= NULL,
#endif
	.journal_pid	= format40_journal_pid,
	.alloc_pid	= format40_alloc_pid,
	.oid_pid	= format40_oid_pid
    }
};

static reiser4_plugin_t *format40_start(reiser4_core_t *c) {
    core = c;
    return &format40_plugin;
}

libreiser4_factory_register(format40_start);

