/*
    format36.c -- Disk-layout plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format36.h"

static reiserfs_plugin_factory_t *factory = NULL;

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

static int format36_signature(format36_super_t *super) {
    const char *signature = (const char *)super->s_v1.sb_magic;

    if (format36_3_5_signature(signature) ||
	    format36_3_6_signature(signature) ||
	    format36_journal_signature(signature))
	return 1;

    return 0;
}

static error_t format36_super_check(format36_super_t *super, 
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
    format36_super_t *super;
    int i, super_offset[] = {16, 2, -1};

    blocksize = aal_device_get_bs(device);
    aal_device_set_bs(device, REISERFS_DEFAULT_BLOCKSIZE);
    
    for (i = 0; super_offset[i] != -1; i++) {
	if ((block = aal_block_read(device, super_offset[i]))) {
	    super = (format36_super_t *)block->data;
			
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
	}
    }
    return NULL;
}

static format36_t *format36_open(aal_device_t *host_device, 
    aal_device_t *journal_device) 
{
    format36_t *format;

    aal_assert("umka-380", host_device != NULL, return NULL);    
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
		
    if (!(format->super = format36_super_open(host_device)))
	goto error_free_format;
	
    format->device = host_device;
    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t format36_sync(format36_t *format) {
    aal_assert("umka-381", format != NULL, return -1);    
    aal_assert("umka-382", format->super != NULL, return -1);    

    if (aal_block_write(format->device, format->super)) {
    	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't write superblock to block %llu.", 
	    aal_block_get_nr(format->super));
	return -1;
    }
    return 0;
}

static format36_t *format36_create(aal_device_t *host_device, 
    count_t blocks, aal_device_t *journal_device, reiserfs_params_opaque_t *params)
{
    return NULL;
}

static error_t format36_check(format36_t *format) {
    
    aal_assert("umka-383", format != NULL, return -1);
    
    return format36_super_check((format36_super_t *)format->super->data, 
	format->device);
}

static void format36_close(format36_t *format) {
    
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

static const char *format36_format(format36_t *format) {
    format36_super_t *super = (format36_super_t *)format->super->data;
    int version = get_sb_format(super);
    return formats[version >= 0 && version < 3 ? version : 1];
}

static reiserfs_plugin_id_t format36_journal_plugin(format36_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t format36_alloc_plugin(format36_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t format36_oid_plugin(format36_t *format) {
    return 0x1;
}

static blk_t format36_offset(format36_t *format) {
    aal_assert("umka-386", format != NULL, return 0);
    return (REISERFS_MASTER_OFFSET / aal_device_get_bs(format->device));
}

static reiserfs_opaque_t *format36_journal(format36_t *format) {
    aal_assert("umka-488", format != NULL, return 0);
    return format->journal;
}

static reiserfs_opaque_t *format36_alloc(format36_t *format) {
    aal_assert("umka-506", format != NULL, return 0);
    return format->alloc;
}

static reiserfs_opaque_t *format36_oid(format36_t *format) {
    aal_assert("umka-507", format != NULL, return 0);
    return format->oid;
}

static blk_t format36_get_root(format36_t *format) {
    aal_assert("umka-387", format != NULL, return 0);
    return get_sb_root_block((format36_super_t *)format->super->data);
}

static count_t format36_get_blocks(format36_t *format) {
    aal_assert("umka-388", format != NULL, return 0);
    return get_sb_block_count((format36_super_t *)format->super->data);
}

static count_t format36_get_free(format36_t *format) {
    aal_assert("umka-389", format != NULL, return 0);
    return get_sb_free_blocks((format36_super_t *)format->super->data);
}

static void format36_set_root(format36_t *format, blk_t root) {
    aal_assert("umka-390", format != NULL, return);
    set_sb_root_block((format36_super_t *)format->super->data, root);
}

static void format36_set_blocks(format36_t *format, count_t blocks) {
    aal_assert("umka-391", format != NULL, return);
    set_sb_block_count((format36_super_t *)format->super->data, blocks);
}

static void format36_set_free(format36_t *format, count_t blocks) {
    aal_assert("umka-392", format != NULL, return);
    set_sb_free_blocks((format36_super_t *)format->super->data, blocks);
}

static reiserfs_plugin_t format36_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "format36",
	    .desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, aal_device_t *))
	    format36_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t, 
	    aal_device_t *, reiserfs_params_opaque_t *))format36_create,
	
	.close = (void (*)(reiserfs_opaque_t *))format36_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))format36_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))format36_check,
	.confirm = (int (*)(aal_device_t *))format36_confirm,
	.format = (const char *(*)(reiserfs_opaque_t *))format36_format,

	.offset = (blk_t (*)(reiserfs_opaque_t *))format36_offset,
	
	.get_root = (blk_t (*)(reiserfs_opaque_t *))format36_get_root,
	.set_root = (void (*)(reiserfs_opaque_t *, blk_t))format36_set_root,
	
	.get_blocks = (count_t (*)(reiserfs_opaque_t *))format36_get_blocks,
	.set_blocks = (void (*)(reiserfs_opaque_t *, count_t))format36_set_blocks,
	
	.get_free = (count_t (*)(reiserfs_opaque_t *))format36_get_free,
	.set_free = (void (*)(reiserfs_opaque_t *, count_t))format36_set_free,
	
	.get_height = NULL,
	.set_height = NULL,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format36_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format36_alloc_plugin,
	
	.oid_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format36_oid_plugin,
	
	.journal = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format36_journal,
	.alloc = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format36_alloc,
	.oid = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format36_oid,
    }
};

static reiserfs_plugin_t *format36_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &format36_plugin;
}

libreiser4_plugins_register(format36_entry);

