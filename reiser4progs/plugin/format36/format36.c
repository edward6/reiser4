/*
    format36.c -- Disk-layout plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

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

static aal_block_t *reiserfs_format36_super_init(aal_device_t *device) {
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

static reiserfs_format36_t *reiserfs_format36_init(aal_device_t *host_device, 
    aal_device_t *journal_device) 
{
    reiserfs_format36_t *format;

    aal_assert("umka-380", host_device != NULL, return NULL);    
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
		
    if (!(format->super = reiserfs_format36_super_init(host_device)))
	goto error_free_format;
	
    format->device = host_device;
    return format;

error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format36_sync(reiserfs_format36_t *format) {
    aal_assert("umka-381", format != NULL, return -1);    
    aal_assert("umka-382", format->super != NULL, return -1);    

    if (aal_device_write_block(format->device, format->super)) {
    	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't write superblock to block %llu.", 
	    aal_device_get_block_nr(format->device, format->super));
	return -1;
    }
    return 0;
}

static reiserfs_format36_t *reiserfs_format36_create(aal_device_t *host_device, 
    count_t blocks, aal_device_t *journal_device, reiserfs_params_opaque_t *params)
{
    return NULL;
}

static error_t reiserfs_format36_check(reiserfs_format36_t *format) {
    
    aal_assert("umka-383", format != NULL, return -1);
    
    return reiserfs_format36_super_check((reiserfs_format36_super_t *)format->super->data, 
	format->device);
}

static void reiserfs_format36_fini(reiserfs_format36_t *format) {
    
    aal_assert("umka-384", format != NULL, return);
    
    aal_device_free_block(format->super);
    aal_free(format);
}

static int reiserfs_format36_confirm(aal_device_t *device) {
    aal_block_t *block;
    
    aal_assert("umka-385", device != NULL, return 0);
    
    if (!(block = reiserfs_format36_super_init(device)))
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
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format36_alloc_plugin(reiserfs_format36_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format36_oid_plugin(reiserfs_format36_t *format) {
    return 0x1;
}

static blk_t reiserfs_format36_offset(reiserfs_format36_t *format) {
    aal_assert("umka-386", format != NULL, return 0);
    return (REISERFS_MASTER_OFFSET / aal_device_get_blocksize(format->device));
}

static reiserfs_opaque_t *reiserfs_format36_journal(reiserfs_format36_t *format) {
    aal_assert("umka-488", format != NULL, return 0);
    return format->journal;
}

static reiserfs_opaque_t *reiserfs_format36_alloc(reiserfs_format36_t *format) {
    aal_assert("umka-506", format != NULL, return 0);
    return format->alloc;
}

static reiserfs_opaque_t *reiserfs_format36_oid(reiserfs_format36_t *format) {
    aal_assert("umka-507", format != NULL, return 0);
    return format->oid;
}

static blk_t reiserfs_format36_get_root(reiserfs_format36_t *format) {
    aal_assert("umka-387", format != NULL, return 0);
    return get_sb_root_block((reiserfs_format36_super_t *)format->super->data);
}

static count_t reiserfs_format36_get_blocks(reiserfs_format36_t *format) {
    aal_assert("umka-388", format != NULL, return 0);
    return get_sb_block_count((reiserfs_format36_super_t *)format->super->data);
}

static count_t reiserfs_format36_get_free(reiserfs_format36_t *format) {
    aal_assert("umka-389", format != NULL, return 0);
    return get_sb_free_blocks((reiserfs_format36_super_t *)format->super->data);
}

static void reiserfs_format36_set_root(reiserfs_format36_t *format, blk_t root) {
    aal_assert("umka-390", format != NULL, return);
    set_sb_root_block((reiserfs_format36_super_t *)format->super->data, root);
}

static void reiserfs_format36_set_blocks(reiserfs_format36_t *format, count_t blocks) {
    aal_assert("umka-391", format != NULL, return);
    set_sb_block_count((reiserfs_format36_super_t *)format->super->data, blocks);
}

static void reiserfs_format36_set_free(reiserfs_format36_t *format, count_t blocks) {
    aal_assert("umka-392", format != NULL, return);
    set_sb_free_blocks((reiserfs_format36_super_t *)format->super->data, blocks);
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
	.init = (reiserfs_opaque_t *(*)(aal_device_t *, aal_device_t *))
	    reiserfs_format36_init,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t, 
	    aal_device_t *, reiserfs_params_opaque_t *))reiserfs_format36_create,
	
	.fini = (void (*)(reiserfs_opaque_t *))reiserfs_format36_fini,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_format36_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))reiserfs_format36_check,
	.confirm = (int (*)(aal_device_t *))reiserfs_format36_confirm,
	.format = (const char *(*)(reiserfs_opaque_t *))reiserfs_format36_format,

	.offset = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format36_offset,
	
	.get_root = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format36_get_root,
	.set_root = (void (*)(reiserfs_opaque_t *, blk_t))reiserfs_format36_set_root,
	
	.get_blocks = (count_t (*)(reiserfs_opaque_t *))reiserfs_format36_get_blocks,
	.set_blocks = (void (*)(reiserfs_opaque_t *, count_t))reiserfs_format36_set_blocks,
	
	.get_free = (count_t (*)(reiserfs_opaque_t *))reiserfs_format36_get_free,
	.set_free = (void (*)(reiserfs_opaque_t *, count_t))reiserfs_format36_set_free,
	
	.get_height = NULL,
	.set_height = NULL,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_alloc_plugin,
	
	.oid_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format36_oid_plugin,
	
	.journal = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))reiserfs_format36_journal,
	.alloc = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))reiserfs_format36_alloc,
	.oid = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))reiserfs_format36_oid,
    }
};

reiserfs_plugin_t *reiserfs_format36_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &format36_plugin;
}

reiserfs_plugin_register(reiserfs_format36_entry);

