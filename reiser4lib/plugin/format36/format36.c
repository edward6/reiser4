/*
    format36.c -- Disk-layout plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "format36.h"

static reiserfs_plugins_factory_t *factory = NULL;

static int reiserfs_format36_3_5_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_3_5_SUPER_SIGNATURE,
	aal_strlen(REISERFS_3_5_SUPER_SIGNATURE)));
}

static int reiserfs_format36_3_6_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_3_6_SUPER_SIGNATURE,
	aal_strlen(REISERFS_3_6_SUPER_SIGNATURE)));
}

static int reiserfs_format36_journal_signature(const char *signature) {
    return(!aal_strncmp(signature, REISERFS_JR_SUPER_SIGNATURE,
	aal_strlen(REISERFS_JR_SUPER_SIGNATURE)));
}

static int reiserfs_format36_signature(reiserfs_format36_super_t *super) {
    const char *signature = (const char *)super->s_v1.sb_magic;

    if (reiserfs_format36_3_5_signature(signature) ||
	    reiserfs_format36_3_6_signature(signature) ||
	    reiserfs_format36_journal_signature(signature))
	return 1;

    return 0;
}

static error_t reiserfs_format36_super_check(reiserfs_format36_super_t *super, 
    aal_device_t *device) 
{
    blk_t dev_len;
    int is_journal_dev, is_journal_magic;

    is_journal_dev = (get_jp_dev(get_sb_jp(super)) ? 1 : 0);
    is_journal_magic = reiserfs_format36_journal_signature(super->s_v1.sb_magic);

    if (is_journal_dev != is_journal_magic) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "umka-020", "Journal relocation flags mismatch. Journal device: %x, magic: %s.",
	    get_jp_dev(get_sb_jp(super)), super->s_v1.sb_magic);
    }

    dev_len = aal_device_len(device);
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "umka-021", "Superblock has an invalid block count %d for device "
	    "length %d blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }

    return 0;
}

static aal_block_t *reiserfs_format36_super_open(aal_device_t *device) {
    aal_block_t *block;
    uint16_t blocksize;
    reiserfs_format36_super_t *super;
    int i, super_offset[] = {16, 2, -1};

    blocksize = aal_device_get_blocksize(device);
    aal_device_set_blocksize(device, REISERFS_DEFAULT_BLOCKSIZE);
    
    for (i = 0; super_offset[i] != -1; i++) {
	if ((block = aal_device_read_block(device, super_offset[i]))) {
	    super = (reiserfs_format36_super_t *)block->data;
			
	    if (reiserfs_format36_signature(super)) {
		if (aal_device_set_blocksize(device, get_sb_block_size(super))) {
		    aal_device_free_block(block);
		    continue;
		}
				
		if (reiserfs_format36_super_check(super, device)) {
		    aal_device_free_block(block);
		    continue;
		}
		
		if (aal_device_set_blocksize(device, get_sb_block_size(super)))
		    return block;
		
	    }
	    aal_device_free_block(block);
	}
    }
    return NULL;
}

static reiserfs_format36_t *reiserfs_format36_open(reiserfs_opaque_t *alloc, 
    aal_device_t *device) 
{
    reiserfs_format36_t *format;
	
    if (!device)
	return NULL;
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
		
    if (!(format->super = reiserfs_format36_super_open(device)))
	goto error_free_format;
	
    format->device = device;
    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format36_sync(reiserfs_format36_t *format) {
    if (!format || !format->super)
	return -1;

    if (aal_device_write_block(format->device, format->super)) {
    	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't write superblock to %d.", 
	    aal_device_get_block_location(format->device, format->super));
	return -1;
    }
    return 0;
}

static reiserfs_format36_t *reiserfs_format36_create(aal_device_t *device, 
    blk_t offset, count_t blocks, uint16_t blocksize) 
{
    return NULL;
}

static error_t reiserfs_format36_check(reiserfs_format36_t *format) {
    return reiserfs_format36_super_check((reiserfs_format36_super_t *)format->super->data, 
	format->device);
}

static void reiserfs_format36_close(reiserfs_format36_t *format, int sync) {
	
    if (sync)
	reiserfs_format36_sync(format);
    
    aal_device_free_block(format->super);
    aal_free(format);
}

static int reiserfs_format36_probe(aal_device_t *device, blk_t offset) {
    aal_block_t *block;
	
    if (!(block = reiserfs_format36_super_open(device)))
	return 0;
	
    aal_device_free_block(block);
    return 1;
}

static const char *formats[] = {"3.5", "unknown", "3.6"};

static const char *reiserfs_format36_format(reiserfs_format36_t *format) {
    reiserfs_format36_super_t *super = (reiserfs_format36_super_t *)format->super->data;
    int version = get_sb_format(super);
    return formats[version >= 0 && version < 3 ? version : 1];
}

static reiserfs_plugin_id_t reiserfs_format36_journal_plugin(reiserfs_format36_t *format) {
    return 0x2;
}

static reiserfs_plugin_id_t reiserfs_format36_alloc_plugin(reiserfs_format36_t *format) {
    return 0x2;
}

static reiserfs_plugin_id_t reiserfs_format36_node_plugin(reiserfs_format36_t *format) {
    return 0x2;
}

static blk_t reiserfs_format36_root_block(reiserfs_format36_t *format) {
    return get_sb_root_block((reiserfs_format36_super_t *)format->super->data);
}

static reiserfs_plugin_t format36_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "format36",
	    .desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t))reiserfs_format36_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t, count_t, uint16_t))
	    reiserfs_format36_create,
	
	.close = (void (*)(reiserfs_opaque_t *, int))reiserfs_format36_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_format36_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))reiserfs_format36_check,
	.probe = (int (*)(aal_device_t *, blk_t))reiserfs_format36_probe,
	.format = (const char *(*)(reiserfs_opaque_t *))reiserfs_format36_format,
			
	.root_block = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format36_root_block,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_alloc_plugin,
	
	.node_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_node_plugin,
    }
};

reiserfs_plugin_t *reiserfs_format36_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &format36_plugin;
}

reiserfs_plugin_register(reiserfs_format36_entry);

