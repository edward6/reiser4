/*
	format40.c -- default disk-layout plugin for reiserfs 4.0
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "format40.h"

static int reiserfs_format40_super_check(reiserfs_format40_super_t *super, 
	aal_device_t *device) 
{
	blk_t dev_len = aal_device_len(device);
	if (get_sb_block_count(super) > dev_len) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
			"umka-010", "Superblock has an invalid block count %d for device "
			"length %d blocks.", get_sb_block_count(super), dev_len);
		return 0;
	}
	return 1;
}

static int reiserfs_format40_signature(reiserfs_format40_super_t *super) {
	return aal_strncmp(super->sb_magic, REISERFS_FORMAT40_MAGIC, 16) == 0;
}

static aal_block_t *reiserfs_format40_super_open(aal_device_t *device) {
	aal_block_t *block;
	reiserfs_format40_super_t *super;
	int i, super_offset[] = {17, -1};
	
	for (i = 0; super_offset[i] != -1; i++) {
		if ((block = aal_block_read(device, super_offset[i]))) {
			super = (reiserfs_format40_super_t *)block->data;
			
			if (reiserfs_format40_signature(super)) {
				if (!reiserfs_format40_super_check(super, device)) {
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

static reiserfs_format40_t *reiserfs_format40_init(aal_device_t *device) {
	reiserfs_format40_t *format;
	
	if (!device)
		return NULL;
	
	if (!(format = aal_calloc(sizeof(*format), 0)))
		return NULL;

	if (!(format->super = reiserfs_format40_super_open(device)))
		goto error_free_format;
		
	format->device = device;
	return format;
	
error_free_format:
	aal_free(format);
error:
	return NULL;
}

static void reiserfs_format40_done(reiserfs_format40_t *format) {
	if (!aal_block_write(format->device, format->super)) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-009", 
			"Can't synchronize super block.");
	}
	aal_block_free(format->super);
	aal_free(format);
}

static reiserfs_plugin_id_t reiserfs_format40_journal_plugin(reiserfs_format40_t *format) {
	return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format40_alloc_plugin(reiserfs_format40_t *format) {
	return 0x1;
}

reiserfs_plugin_t plugin_info = {
	.format = {
		.h = {
			.handle = NULL,
			.id = 0x1,
			.type = REISERFS_FORMAT_PLUGIN,
			.label = "format40",
			.desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
		},
		.init = (reiserfs_format_opaque_t *(*)(aal_device_t *))reiserfs_format40_init,
		.done = (void (*)(reiserfs_format_opaque_t *))reiserfs_format40_done,
		
		.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
			reiserfs_format40_journal_plugin,
		
		.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_format_opaque_t *))
			reiserfs_format40_alloc_plugin
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

