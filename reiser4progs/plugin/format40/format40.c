/*
    format40.c -- default disk-layout plugin for reiser4.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include "format40.h"

#ifndef ENABLE_COMPACT
#  include <stdlib.h>
#  include <time.h>
#endif

extern reiser4_plugin_t format40_plugin;

static reiser4_core_t *core = NULL;

static blk_t format40_get_root(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-400", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return (blk_t)get_sb_root_block(super);
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
    
    aal_assert("umka-1123", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_tree_height(super);
}

static uint32_t format40_get_stamp(reiser4_entity_t *entity) {
    format40_super_t *super;
    
    aal_assert("umka-1122", entity != NULL, return 0);
    
    super = format40_super(((format40_t *)entity)->block);
    return get_sb_mkfs_id(super);
}

#define FORMAT40_JHEADER (4096 * 19)
#define FORMAT40_JFOOTER (4096 * 20)

/* This function describes journal layout in format40 */
static errno_t format40_journal_layout(reiser4_entity_t *entity,
    reiser4_action_func_t action_func, void *data)
{
    blk_t blk;
    format40_t *format = (format40_t *)entity;
    
    aal_assert("umka-1040", format != NULL, return -1);
    aal_assert("umka-1041", action_func != NULL, return -1);
    
    blk = FORMAT40_JHEADER / aal_device_get_bs(format->device);
    
    if (action_func((reiser4_entity_t *)format, blk, data))
	return -1;
    
    blk = FORMAT40_JFOOTER / aal_device_get_bs(format->device);
    
    if (action_func((reiser4_entity_t *)format, blk, data))
	return -1;

    return 0;
}

#define FORMAT40_ALLOC (REISER4_MASTER_OFFSET + (4096 * 2))

static errno_t format40_alloc_layout(reiser4_entity_t *entity,
    reiser4_action_func_t action_func, void *data) 
{
    blk_t blk, start;
    format40_t *format = (format40_t *)entity;
	
    aal_assert("umka-347", entity != NULL, return -1);
    aal_assert("umka-348", action_func != NULL, return -1);

    start = FORMAT40_ALLOC / aal_device_get_bs(format->device);
    
    for (blk = start; blk < format40_get_len(entity);) {	
	
	if (action_func((reiser4_entity_t *)format, blk, data))
	    return -1;
	
	blk = (blk / (aal_device_get_bs(format->device) * 8) + 1) * 
	    (aal_device_get_bs(format->device) * 8);
    }
    
    return 0;
}

static errno_t format40_skipped_layout(reiser4_entity_t *entity,
    reiser4_action_func_t action_func, void *data) 
{
    blk_t blk, offset;
    format40_t *format = (format40_t *)entity;
        
    aal_assert("umka-1085", entity != NULL, return -1);
    aal_assert("umka-1086", action_func != NULL, return -1);
    
    offset = REISER4_MASTER_OFFSET / format->device->blocksize;
    
    for (blk = 0; blk < offset; blk++) {
	if (action_func((reiser4_entity_t *)format, blk, data))
	    return -1;
    }
    
    return 0;
}

static errno_t format40_format_layout(reiser4_entity_t *entity,
    reiser4_action_func_t action_func, void *data) 
{
    blk_t blk, offset;
    format40_t *format = (format40_t *)entity;
        
    aal_assert("umka-1042", entity != NULL, return -1);
    aal_assert("umka-1043", action_func != NULL, return -1);
    
    blk = REISER4_MASTER_OFFSET / format->device->blocksize;
    offset = FORMAT40_OFFSET / format->device->blocksize;
    
    for (; blk <= offset; blk++) {
	if (action_func((reiser4_entity_t *)format, blk, data))
	    return -1;
    }
    
    return 0;
}

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
	aal_exception_error(
	    "Superblock has an invalid root block %llu for device "
	    "length %llu blocks.", get_sb_root_block(super), dev_len);
	return -1;
    }
    return 0;
}

static int format40_magic(format40_super_t *super) {
    return aal_strncmp(super->sb_magic, FORMAT40_MAGIC, 
	aal_strlen(FORMAT40_MAGIC)) == 0;
}

static aal_device_t *format40_device(reiser4_entity_t *entity) {
    return ((format40_t *)entity)->device;
}

static aal_block_t *format40_super_open(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    
    offset = (FORMAT40_OFFSET / aal_device_get_bs(device));
	
    if (!(block = aal_block_open(device, offset))) {
	aal_exception_error("Can't read block %llu. %s.", offset, 
	   aal_device_error(device));
	return NULL;
    }

    if (!format40_magic((format40_super_t *)block->data))
	return NULL;
    
    return block;
}

static reiser4_entity_t *format40_open(aal_device_t *device) {
    format40_t *format;

    aal_assert("umka-393", device != NULL, return NULL);

    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    format->device = device;
    format->plugin = &format40_plugin;
    
    if (!(format->block = format40_super_open(device)))
	goto error_free_format;
    
    return (reiser4_entity_t *)format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static errno_t callback_clobber_block(reiser4_entity_t *entity, 
    blk_t blk, void *data) 
{
    aal_block_t *block;
    format40_t *format;

    format = (format40_t *)entity;
    
    if (!(block = aal_block_create(format->device, blk, 0))) {
	aal_exception_error("Can't clobber block %llu.", blk);
	return -1;
    }
    
    if (aal_block_sync(block)) {
	aal_exception_error("Can't write block %llu to device. %s.", blk, 
	    format->device->error);
	goto error_free_block;
    }
    
    aal_block_free(block);
    return 0;
    
error_free_block:
    aal_block_free(block);
    return -1;
}

/* This function should create super block and update all copies */
static reiser4_entity_t *format40_create(aal_device_t *device, 
    count_t blocks, uint16_t tail)
{
    blk_t blk;
    format40_t *format;
    format40_super_t *super;
    
    aal_assert("umka-395", device != NULL, return NULL);
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;
    format->plugin = &format40_plugin;

    if (!(format->block = aal_block_create(device, (FORMAT40_OFFSET / 
	aal_device_get_bs(device)), 0))) 
    {
	aal_exception_error("Can't allocate superblock.");
	goto error_free_format;
    }
    
    super = (format40_super_t *)format->block->data;
    aal_memcpy(super->sb_magic, FORMAT40_MAGIC, 
	aal_strlen(FORMAT40_MAGIC));

    set_sb_block_count(super, blocks);
    set_sb_tree_height(super, 2);
    set_sb_flushes(super, 0);
    set_sb_tail_policy(super, tail);

    srandom(time(0));
    set_sb_mkfs_id(super, random());

    /* Clobbering skipped area */
    if (format40_skipped_layout((reiser4_entity_t *)format, 
        callback_clobber_block, NULL))
    {
	aal_exception_error("Can't clobber skipped area.");
	goto error_free_block;
    }
    
    return (reiser4_entity_t *)format;

error_free_block:
    aal_block_free(format->block);
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
    
    if (aal_block_sync(format->block)) {
	blk_t offset = aal_block_number(format->block);
	
	aal_exception_error("Can't write superblock to %llu. %s.", offset, 
	    aal_device_error(format->device));
	
	return -1;
    }
    
    return 0;
}

#endif

static errno_t format40_valid(reiser4_entity_t *entity) {
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

static const char *formats[] = {"4.0"};

static const char *format40_name(reiser4_entity_t *entity) {
    return formats[0];
}

static rpid_t format40_journal_pid(reiser4_entity_t *entity) {
    return JOURNAL_REISER40_ID;
}

static rpid_t format40_alloc_pid(reiser4_entity_t *entity) {
    return ALLOC_REISER40_ID;
}

static rpid_t format40_oid_pid(reiser4_entity_t *entity) {
    return OID_REISER40_ID;
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

static void format40_set_stamp(reiser4_entity_t *entity, 
    uint32_t mkfsid) 
{
    format40_super_t *super;
    
    aal_assert("umka-1121", entity != NULL, return);

    super = format40_super(((format40_t *)entity)->block);
    set_sb_mkfs_id(super, mkfsid);
}

extern errno_t format40_check(reiser4_entity_t *entity, 
    uint16_t options);

extern errno_t format40_print(reiser4_entity_t *entity, 
    char *buff, uint32_t n, uint16_t options);

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
	.device		= format40_device,
#ifndef ENABLE_COMPACT	
	.check		= format40_check,
	.sync		= format40_sync,
	.create		= format40_create,
	.print		= format40_print,
#else
	.check		= NULL,
	.sync		= NULL,
	.create		= NULL,
	.print		= NULL,
#endif
	.oid_area	= format40_oid_area,
	
	.close		= format40_close,
	.confirm	= format40_confirm,
	.name		= format40_name,
	
	.get_root	= format40_get_root,
	.get_len	= format40_get_len,
	.get_free	= format40_get_free,
	.get_height	= format40_get_height,
	.get_stamp	= format40_get_stamp,

	.skipped_layout	= format40_skipped_layout,
	.format_layout	= format40_format_layout,
	.alloc_layout	= format40_alloc_layout,
	.journal_layout	= format40_journal_layout,
	
#ifndef ENABLE_COMPACT	
	.set_root	= format40_set_root,
	.set_len	= format40_set_len,
	.set_free	= format40_set_free,
	.set_height	= format40_set_height,
	.set_stamp	= format40_set_stamp,
#else
	.set_root	= NULL,
	.set_len	= NULL,
	.set_free	= NULL,
	.set_height	= NULL,
	.set_stamp	= NULL,
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

plugin_register(format40_start);

