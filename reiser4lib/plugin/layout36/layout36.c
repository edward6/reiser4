/*
	layout36.c -- disk-layout plugin for reiserfs 3.6.x
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <aal/aal.h>
#include <reiserfs/plugin.h>

#include <stdlib.h>

#include "layout36.h"

static int reiserfs_layout36_3_5_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_3_5_SUPER_SIGNATURE,
		strlen(REISERFS_3_5_SUPER_SIGNATURE)));
}

static int reiserfs_layout36_3_6_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_3_6_SUPER_SIGNATURE,
		strlen(REISERFS_3_6_SUPER_SIGNATURE)));
}

static int reiserfs_layout36_journal_signature(const char *signature) {
	return(!aal_strncmp(signature, REISERFS_JR_SUPER_SIGNATURE,
		strlen(REISERFS_JR_SUPER_SIGNATURE)));
}

static int reiserfs_layout36_any_signature(const char *signature) {
	if (reiserfs_layout36_3_5_signature(signature) ||
			reiserfs_layout36_3_6_signature(signature) ||
			reiserfs_layout36_journal_signature(signature))
		return 1;

	return 0;
}

static aal_block_t *reiserfs_layout36_super_open(aal_device_t *device) {
	aal_block_t *block;
	reiserfs_layout36_super_t *super;
	int i, super_offset[] = {16, 2, -1};

	for (i = 0; super_offset[i] != -1; i++) {
		if ((block = aal_block_read(device, super_offset[i]))) {
			super = (reiserfs_layout36_super_t *)block->data;
			if (reiserfs_layout36_any_signature((const char *)super->s_v1.sb_magic)) {
				
//				size_t blocksize = get_sb_block_size(super);
				size_t blocksize = super->s_v1.sb_block_size;
				
				if (!aal_device_set_blocksize(device, blocksize)) {
					aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
						"umka-005", "Invalid blocksize %d. It must power of two.", blocksize);
					aal_block_free(block);
					continue;
				}
/*				if (!reiserfs_layout36_super_check(super, aal_device_len(device))) {
					aa_block_free(block);
					continue;
				}*/
				return block;
			}
			aal_block_free(block);
		}
	}
	return NULL;
}

static reiserfs_layout36_t *reiserfs_layout36_init(aal_device_t *device) {
	aal_block_t *block;
	reiserfs_layout36_t *layout;
	
	if (!device)
		return NULL;
	
	if (!(block = reiserfs_layout36_super_open(device)))
		return NULL;
	
	if (!(layout = aal_calloc(sizeof(*layout), 0)))
		return NULL;
		
	layout->device = device;
	
	if (!(layout->super = aal_calloc(aal_device_blocksize(device), 0)))
		goto error_free_layout;

	aal_memcpy(layout->super, block->data, aal_device_blocksize(device));
	
	return layout;

error_free_layout:
	aal_free(layout);
error_free_block:
	aal_block_free(block);
error:
	return NULL;
}

static void reiserfs_layout36_done(reiserfs_layout36_t *layout) {
	/* Syncing superblock and other */
	aal_free(layout->super);
	aal_free(layout);
}

reiserfs_plugin_t plugin_info = {
	.layout = {
		.h = {
			.handle = NULL,
			.id = 0x2,
			.type = REISERFS_LAYOUT_PLUGIN,
			.label = "layout36",
			.desc = "Disk-layout for reiserfs 3.6.x, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
			.nlink = 0
		},
		.init = (reiserfs_layout_opaque_t *(*)(aal_device_t *))reiserfs_layout36_init,
		.done = (void (*)(reiserfs_layout_opaque_t *))reiserfs_layout36_done
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

