/*
	layout40.c -- default disk-layout plugin for reiserfs 4.0
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "layout40.h"

static int reiserfs_layout40_super_check(reiserfs_layout40_super_t *super, 
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

static int reiserfs_layout40_signature(reiserfs_layout40_super_t *super) {
	return 1;
}

static aal_block_t *reiserfs_layout40_super_open(aal_device_t *device) {
	aal_block_t *block;
	reiserfs_layout40_super_t *super;
	int i, super_offset[] = {16, 2, -1};
	
    for (i = 0; super_offset[i] != -1; i++) {
		if ((block = aal_block_read(device, super_offset[i]))) {
			super = (reiserfs_layout40_super_t *)block->data;
			
			if (reiserfs_layout40_signature(super)) {
				size_t blocksize = get_sb_block_size(super);
				if (!aal_device_set_blocksize(device, blocksize)) {
					aal_block_free(block);
					continue;
				}
				if (!reiserfs_layout40_super_check(super, device)) {
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

static reiserfs_layout40_t *reiserfs_layout40_init(aal_device_t *device) {
	reiserfs_layout40_t *layout;
	
	if (!device)
		return NULL;
	
	if (!(layout = aal_calloc(sizeof(*layout), 0)))
		return NULL;

	if (!(layout->super = reiserfs_layout40_super_open(device)))
		goto error_free_layout;
		
	layout->device = device;
	return layout;
	
error_free_layout:
	aal_free(layout);
error:
	return NULL;
}

static void reiserfs_layout40_done(reiserfs_layout40_t *layout) {
	if (!aal_block_write(layout->device, layout->super)) {
		aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-009", 
			"Can't synchronize super block.");
	}
	aal_block_free(layout->super);
	aal_free(layout);
}

static reiserfs_plugin_id_t reiserfs_layout40_journal_plugin(reiserfs_layout40_t *layout) {
	return 0x1;
}

static reiserfs_plugin_id_t reiserfs_layout40_alloc_plugin(reiserfs_layout40_t *layout) {
	return 0x1;
}

reiserfs_plugin_t plugin_info = {
	.layout = {
		.h = {
			.handle = NULL,
			.id = 0x1,
			.type = REISERFS_LAYOUT_PLUGIN,
			.label = "layout40",
			.desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
		},
		.init = (reiserfs_layout_opaque_t *(*)(aal_device_t *))reiserfs_layout40_init,
		.done = (void (*)(reiserfs_layout_opaque_t *))reiserfs_layout40_done,
		
		.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_layout_opaque_t *))
			reiserfs_layout40_journal_plugin,
		
		.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_layout_opaque_t *))
			reiserfs_layout40_alloc_plugin
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

