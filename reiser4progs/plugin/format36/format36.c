/*
    format36.c -- Disk-layout plugin for reiser3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format36.h"

static reiser4_core_t *core = NULL;

static int format36_35_magic(const char *magic) {
    return (!aal_strncmp(magic, FORMAT36_35_MAGIC, aal_strlen(FORMAT36_35_MAGIC)));
}

static int format36_36_magic(const char *magic) {
    return (!aal_strncmp(magic, FORMAT36_36_MAGIC, aal_strlen(FORMAT36_36_MAGIC)));
}

static int format36_jr_magic(const char *magic) {
    return (!aal_strncmp(magic, FORMAT36_JR_MAGIC, aal_strlen(FORMAT36_JR_MAGIC)));
}

static int format36_magic(format36_super_t *super) {
    const char *magic = (const char *)super->s_v1.sb_magic;

    if (format36_35_magic(magic) || format36_36_magic(magic) || 
	    format36_jr_magic(magic))
	return 1;
    
    return 0;
}

static errno_t format36_super_check(format36_super_t *super, 
    aal_device_t *device) 
{
    blk_t dev_len;
    int is_journal_dev, is_journal_magic;

    is_journal_dev = (get_jp_dev(get_sb_jp(super)) ? 1 : 0);
    is_journal_magic = format36_jr_magic(super->s_v1.sb_magic);

    if (is_journal_dev != is_journal_magic) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Journal relocation flags mismatch. Journal device: %x, magic: %s.",
	    get_jp_dev(get_sb_jp(super)), super->s_v1.sb_magic);
    }

    dev_len = aal_device_len(device);
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %llu for device "
	    "length %llu blocks.", (blk_t)get_sb_block_count(super), dev_len);
	return -1;
    }

    return 0;
}

static aal_block_t *format36_super_open(aal_device_t *device) {
    aal_block_t *block;
    uint16_t blocksize;
    format36_super_t *super;
    int i, super_offset[] = {16, 2, -1};

    blocksize = aal_device_get_bs(device);
    aal_device_set_bs(device, REISER4_DEFAULT_BLOCKSIZE);
    
    for (i = 0; super_offset[i] != -1; i++) {
	if ((block = aal_block_read(device, super_offset[i]))) {
	    super = (format36_super_t *)block->data;
			
	    if (format36_magic(super)) {
		if (aal_device_set_bs(device, get_sb_block_size(super))) {
		    aal_block_free(block);
		    continue;
		}
				
		if (format36_super_check(super, device)) {
		    aal_block_free(block);
		    continue;
		}
		
		if (aal_device_set_bs(device, get_sb_block_size(super)))
		    return block;
		
	    }
	    aal_block_free(block);
	} else {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't read block %d. %s.", super_offset[i], 
		aal_device_error(device));
	}
    }
    return NULL;
}

static reiser4_entity_t *format36_open(aal_device_t *device) {
    format36_t *format;

    aal_assert("umka-380", device != NULL, return NULL);    
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
		
    if (!(format->block = format36_super_open(device)))
	goto error_free_format;
	
    format->device = device;
    return (reiser4_entity_t *)format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static errno_t format36_sync(reiser4_entity_t *entity) {
    format36_t *format;
    
    aal_assert("umka-381", entity != NULL, return -1);
    
    format = (format36_t *)entity;
    aal_assert("umka-382", format->block != NULL, return -1);    
    
    if (aal_block_write(format->block)) {
    	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't write superblock to block %llu. %s.", 
	    aal_block_get_nr(format->block), aal_device_error(format->device));
	return -1;
    }
    
    return 0;
}

static reiser4_entity_t *format36_create(aal_device_t *device, 
    count_t blocks, uint16_t drop_policy)
{
    return NULL;
}

#endif

static errno_t format36_valid(reiser4_entity_t *entity, 
    int flags) 
{
    format36_super_t *super;
    
    aal_assert("umka-383", entity != NULL, return -1);
    
    super = format36_super(((format36_t *)entity)->block);
    return format36_super_check(super, ((format36_t *)entity)->device);
}

static void format36_close(reiser4_entity_t *entity) {
    aal_assert("umka-384", entity != NULL, return);
    aal_block_free(((format36_t *)entity)->block);
    aal_free(entity);
}

static int format36_confirm(aal_device_t *device) {
    aal_block_t *block;
    
    aal_assert("umka-385", device != NULL, return 0);
    
    if (!(block = format36_super_open(device)))
	return 0;
	
    aal_block_free(block);
    return 1;
}

static const char *formats[] = {"3.5", "unknown", "3.6"};

static const char *format36_name(reiser4_entity_t *entity) {
    int version;
    format36_super_t *super;

    aal_assert("umka-1015", entity != NULL, return NULL);
    
    super = format36_super(((format36_t *)entity)->block);
    version = get_sb_format(super);
    
    return formats[version >= 0 && version < 3 ? version : 1];
}

static reiser4_id_t format36_journal_pid(reiser4_entity_t *entity) {
    return JOURNAL_REISER36_ID;
}

static reiser4_id_t format36_alloc_pid(reiser4_entity_t *entity) {
    return ALLOC_REISER36_ID;
}

static reiser4_id_t format36_oid_pid(reiser4_entity_t *entity) {
    return OID_REISER36_ID;
}

static blk_t format36_offset(reiser4_entity_t *entity) {
    aal_assert("umka-386", entity != NULL, return 0);
    return (FORMAT36_OFFSET / aal_device_get_bs(((format36_t *)entity)->device));
}

static blk_t format36_get_root(reiser4_entity_t *entity) {
    format36_super_t *super;
    
    aal_assert("umka-387", entity != NULL, return 0);
    
    super = format36_super(((format36_t *)entity)->block);
    return get_sb_root_block(super);
}

static count_t format36_get_len(reiser4_entity_t *entity) {
    format36_super_t *super;
    
    aal_assert("umka-388", entity != NULL, return 0);
    
    super = format36_super(((format36_t *)entity)->block);
    return get_sb_block_count(super);
}

static count_t format36_get_free(reiser4_entity_t *entity) {
    format36_super_t *super;
    
    aal_assert("umka-389", entity != NULL, return 0);
    
    super = format36_super(((format36_t *)entity)->block);
    return get_sb_free_blocks(super);
}

#ifndef ENABLE_COMPACT

static void format36_set_root(reiser4_entity_t *entity, blk_t root) {
    format36_super_t *super;
    
    aal_assert("umka-390", entity != NULL, return);
    
    super = format36_super(((format36_t *)entity)->block);
    set_sb_root_block(super, root);
}

static void format36_set_len(reiser4_entity_t *entity, 
    count_t blocks)
{
    format36_super_t *super;
    
    aal_assert("umka-391", entity != NULL, return);
    
    super = format36_super(((format36_t *)entity)->block);
    set_sb_block_count(super, blocks);
}

static void format36_set_free(reiser4_entity_t *entity, 
    count_t blocks) 
{
    format36_super_t *super;
    
    aal_assert("umka-392", entity != NULL, return);

    super = format36_super(((format36_t *)entity)->block);
    set_sb_free_blocks(super, blocks);
}

#endif

static reiser4_plugin_t format36_plugin = {
    .format_ops = {
	.h = {
	    .handle = NULL,
	    .id = FORMAT_REISER40_ID,
	    .type = FORMAT_PLUGIN_TYPE,
	    .label = "format36",
	    .desc = "Disk-format for reiserfs 3.6.x, ver. " VERSION,
	},
	.open		= format36_open,
	.valid		= format36_valid,
#ifndef ENABLE_COMPACT
	.sync		= format36_sync,
	.create		= format36_create,
	.set_root	= format36_set_root,
	.set_len	= format36_set_len,
	.set_free	= format36_set_free,
	.set_height	= NULL,
#else
	.sync		= NULL,
	.create		= NULL,
	.set_root	= NULL,
	.set_len	= NULL,
	.set_free	= NULL,
	.set_height	= NULL,
#endif
	.close		= format36_close,
	.confirm	= format36_confirm,
	.name		= format36_name,

	.offset		= format36_offset,
	.get_root	= format36_get_root,
	.get_len	= format36_get_len,
	.get_free	= format36_get_free,
	
	.journal_pid	= format36_journal_pid,
	.alloc_pid	= format36_alloc_pid,
	.oid_pid	= format36_oid_pid,
	
	.get_height	= NULL,
	.oid_area	= NULL,
	.journal_area	= NULL
    }
};

static reiser4_plugin_t *format36_start(reiser4_core_t *c) {
    core = c;
    return &format36_plugin;
}

libreiser4_factory_register(format36_start);

