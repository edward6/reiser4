/*
	format36.c -- Disk-layout plugin for reiserfs 3.6.x
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "format36.h"

static int reiserfs_format36_3_5_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_3_5_SUPER_SIGNATURE,
		strlen(REISERFS_3_5_SUPER_SIGNATURE)));
}

static int reiserfs_format36_3_6_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_3_6_SUPER_SIGNATURE,
		strlen(REISERFS_3_6_SUPER_SIGNATURE)));
}

static int reiserfs_format36_journal_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_JR_SUPER_SIGNATURE,
		strlen(REISERFS_JR_SUPER_SIGNATURE)));
}

static int reiserfs_format36_signature(reiserfs_format36_super_t *super) {
	const char *signature = (const char *)super->s_v1.sb_magic;
	
	if (reiserfs_format36_3_5_signature(signature) ||
			reiserfs_format36_3_6_signature(signature) ||
			reiserfs_format36_journal_signature(signature))
		return 1;

	return 0;
}

static int reiserfs_format36_super_check(reiserfs_format36_super_t *super, 
	aal_device_t *device) 
{
	blk_t dev_len;
	int is_journal_dev, is_journal_magic;

	is_journal_dev = (get_jp_dev(get_sb_jp(super)) ? 1 : 0);
	is_journal_magic = reiserfs_format36_journal_signature(super->s_v1.sb_magic);

	if (is_journal_dev != is_journal_magic) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
			"umka-006", "Journal relocation flags mismatch. Journal device: %x, magic: %s.",
			get_jp_dev(get_sb_jp(super)), super->s_v1.sb_magic);
	}

	dev_len = aal_device_len(device);
	if (get_sb_block_count(super) > dev_len) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
			"umka-007", "Superblock has an invalid block count %d for device "
			"length %d blocks.", get_sb_block_count(super), dev_len);
		return 0;
	}

	return 1;
}

static aal_block_t *reiserfs_format36_super_open(aal_device_t *device) {
	aal_block_t *block;
	reiserfs_format36_super_t *super;
	int i, super_offset[] = {16, 2, -1};

	for (i = 0; super_offset[i] != -1; i++) {
		if ((block = aal_block_read(device, super_offset[i]))) {
			super = (reiserfs_format36_super_t *)block->data;
			if (reiserfs_format36_signature(super)) {

				size_t blocksize = get_sb_block_size(super);
				if (!aal_device_set_blocksize(device, blocksize)) {
					aal_block_free(block);
					continue;
				}
				
				if (!reiserfs_format36_super_check(super, device)) {
					aal_block_free(block);
					continue;
				}
				
				return block;
			}
			aal_block_free(block);
		}
	}
	return NULL;
}

static reiserfs_format36_t *reiserfs_format36_init(aal_device_t *device) {
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

static void reiserfs_format36_done(reiserfs_format36_t *format) {
	if (!aal_block_write(format->device, format->super)) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-008", 
			"Can't synchronize super block.");
	}
	aal_block_free(format->super);
	aal_free(format);
}

static reiserfs_plugin_id_t reiserfs_format36_journal_plugin(reiserfs_format36_t *format) {
	return 0x2;
}

static reiserfs_plugin_id_t reiserfs_format36_alloc_plugin(reiserfs_format36_t *format) {
	return 0x2;
}

reiserfs_plugin_t plugin_info = {
	.format = {
		.h = {
			.handle = NULL,
			.id = 0x2,
			.type = REISERFS_FORMAT_PLUGIN,
			.label = "format36",
			.desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
		},
		.init = (reiserfs_format_opaque_t *(*)(aal_device_t *))reiserfs_format36_init,
		.done = (void (*)(reiserfs_format_opaque_t *))reiserfs_format36_done,
			
		.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
			reiserfs_format36_journal_plugin,
		
		.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
			reiserfs_format36_alloc_plugin
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

