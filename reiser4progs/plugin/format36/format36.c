/*
    format36.c -- Disk-layout plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format36.h"

static reiserfs_core_t *core = NULL;

static int format36_3_5_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_3_5_SUPER_SIGNATURE,
	aal_strlen(REISERFS_3_5_SUPER_SIGNATURE)));
}

static int format36_3_6_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_3_6_SUPER_SIGNATURE,
	aal_strlen(REISERFS_3_6_SUPER_SIGNATURE)));
}

static int format36_journal_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_JR_SUPER_SIGNATURE,
	aal_strlen(REISERFS_JR_SUPER_SIGNATURE)));
}

static int format36_signature(reiserfs_format36_super_t *super) {
    const char *signature = (const char *)super->s_v1.sb_magic;

    if (format36_3_5_signature(signature) ||
	    format36_3_6_signature(signature) ||
	    format36_journal_signature(signature))
	return 1;
    
    return 0;
}

static errno_t format36_super_check(reiserfs_format36_super_t *super, 
    aal_device_t *device) 
{
    blk_t dev_len;
    int is_journal_dev, is_journal_magic;

    is_journal_dev = (get_jp_dev(get_sb_jp(super)) ? 1 : 0);
    is_journal_magic = format36_journal_signature(super->s_v1.sb_magic);

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
    reiserfs_format36_super_t *super;
    int i, super_offset[] = {16, 2, -1};

    blocksize = aal_device_get_bs(device);
    aal_device_set_bs(device, REISERFS_DEFAULT_BLOCKSIZE);
    
    for (i = 0; super_offset[i] != -1; i++) {
	if ((block = aal_block_read(device, super_offset[i]))) {
	    super = (reiserfs_format36_super_t *)block->data;
			
	    if (format36_signature(super)) {
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

static reiserfs_format36_t *format36_open(aal_device_t *device) 
{
    reiserfs_format36_t *format;

    aal_assert("umka-380", device != NULL, return NULL);    
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
		
    if (!(format->super = format36_super_open(device)))
	goto error_free_format;
	
    format->device = device;
    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static errno_t format36_sync(reiserfs_format36_t *format) {
    aal_assert("umka-381", format != NULL, return -1);    
    aal_assert("umka-382", format->super != NULL, return -1);    

    if (aal_block_write(format->super)) {
    	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't write superblock to block %llu. %s.", 
	    aal_block_get_nr(format->super), aal_device_error(format->device));
	return -1;
    }
    return 0;
}

static reiserfs_format36_t *format36_create(aal_device_t *device, 
    count_t blocks, uint16_t drop_policy)
{
    return NULL;
}

#endif

static errno_t format36_check(reiserfs_format36_t *format, int flags) {
    
    aal_assert("umka-383", format != NULL, return -1);
    
    return format36_super_check((reiserfs_format36_super_t *)format->super->data, 
	format->device);
}

static void format36_close(reiserfs_format36_t *format) {
    
    aal_assert("umka-384", format != NULL, return);
    
    aal_block_free(format->super);
    aal_free(format);
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

static const char *format36_format(reiserfs_format36_t *format) {
    reiserfs_format36_super_t *super = (reiserfs_format36_super_t *)format->super->data;
    int version = get_sb_format(super);
    return formats[version >= 0 && version < 3 ? version : 1];
}

static reiserfs_id_t format36_journal_plugin(reiserfs_format36_t *format) {
    return JOURNAL_REISER36_ID;
}

static reiserfs_id_t format36_alloc_plugin(reiserfs_format36_t *format) {
    return ALLOC_REISER36_ID;
}

static reiserfs_id_t format36_oid_plugin(reiserfs_format36_t *format) {
    return OID_REISER36_ID;
}

static blk_t format36_offset(reiserfs_format36_t *format) {
    aal_assert("umka-386", format != NULL, return 0);
    return (REISERFS_MASTER_OFFSET / aal_device_get_bs(format->device));
}

static blk_t format36_get_root(reiserfs_format36_t *format) {
    aal_assert("umka-387", format != NULL, return 0);
    return get_sb_root_block((reiserfs_format36_super_t *)format->super->data);
}

static count_t format36_get_blocks(reiserfs_format36_t *format) {
    aal_assert("umka-388", format != NULL, return 0);
    return get_sb_block_count((reiserfs_format36_super_t *)format->super->data);
}

static count_t format36_get_free(reiserfs_format36_t *format) {
    aal_assert("umka-389", format != NULL, return 0);
    return get_sb_free_blocks((reiserfs_format36_super_t *)format->super->data);
}

#ifndef ENABLE_COMPACT

static void format36_set_root(reiserfs_format36_t *format, blk_t root) {
    aal_assert("umka-390", format != NULL, return);
    set_sb_root_block((reiserfs_format36_super_t *)format->super->data, root);
}

static void format36_set_blocks(reiserfs_format36_t *format, count_t blocks) {
    aal_assert("umka-391", format != NULL, return);
    set_sb_block_count((reiserfs_format36_super_t *)format->super->data, blocks);
}

static void format36_set_free(reiserfs_format36_t *format, count_t blocks) {
    aal_assert("umka-392", format != NULL, return);
    set_sb_free_blocks((reiserfs_format36_super_t *)format->super->data, blocks);
}

#endif

static reiserfs_plugin_t format36_plugin = {
    .format_ops = {
	.h = {
	    .handle = NULL,
	    .id = FORMAT_REISER40_ID,
	    .type = FORMAT_PLUGIN_TYPE,
	    .label = "format36",
	    .desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_entity_t *(*)(aal_device_t *))
	    format36_open,

#ifndef ENABLE_COMPACT
	.sync = (errno_t (*)(reiserfs_entity_t *))format36_sync,
	
	.create = (reiserfs_entity_t *(*)(aal_device_t *, count_t, uint16_t))
	    format36_create,
	
	.check = (errno_t (*)(reiserfs_entity_t *, int))format36_check,
	
	.set_root = (void (*)(reiserfs_entity_t *, blk_t))format36_set_root,
	.set_blocks = (void (*)(reiserfs_entity_t *, count_t))format36_set_blocks,
	.set_free = (void (*)(reiserfs_entity_t *, count_t))format36_set_free,
	.set_height = NULL,
#else
	.sync = NULL,
	.create = NULL,
	.check = NULL,
	
	.set_root = NULL,
	.set_blocks = NULL,
	.set_free = NULL,
	.set_height = NULL,
#endif
	.close = (void (*)(reiserfs_entity_t *))format36_close,
	.confirm = (int (*)(aal_device_t *))format36_confirm,
	.format = (const char *(*)(reiserfs_entity_t *))format36_format,

	.offset = (blk_t (*)(reiserfs_entity_t *))format36_offset,
	
	.get_root = (blk_t (*)(reiserfs_entity_t *))format36_get_root,
	
	.get_blocks = (count_t (*)(reiserfs_entity_t *))format36_get_blocks,
	
	.get_free = (count_t (*)(reiserfs_entity_t *))format36_get_free,
	
	.get_height = NULL,
	
	.oid = NULL,
	
	.journal_pid = (reiserfs_id_t(*)(reiserfs_entity_t *))
	    format36_journal_plugin,
		
	.alloc_pid = (reiserfs_id_t(*)(reiserfs_entity_t *))
	    format36_alloc_plugin,
	
	.oid_pid = (reiserfs_id_t(*)(reiserfs_entity_t *))
	    format36_oid_plugin,
    }
};

static reiserfs_plugin_t *format36_entry(reiserfs_core_t *c) {
    core = c;
    return &format36_plugin;
}

libreiser4_factory_register(format36_entry);

