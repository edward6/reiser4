/*
    format40.c -- default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "format40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_format40_super_check(reiserfs_format40_super_t *super, 
    aal_device_t *device) 
{
    blk_t dev_len = aal_device_len(device);
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %d for device "
	    "length %d blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }
    return 0;
}

static int reiserfs_format40_signature(reiserfs_format40_super_t *super) {
    return aal_strncmp(super->sb_magic, 
	REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC)) == 0;
}

static aal_block_t *reiserfs_format40_super_open(aal_device_t *device, blk_t offset) {
    aal_block_t *block;
    reiserfs_format40_super_t *super;
	
    if (!(block = aal_device_read_block(device, offset))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't read block %d.", offset);
	return NULL;
    }
    super = (reiserfs_format40_super_t *)block->data;
    
    if (!reiserfs_format40_signature(super))
	return NULL;
    
    if (reiserfs_format40_super_check(super, device)) {
        aal_device_free_block(block);
        return NULL;
    }
    
    return block;
}

static reiserfs_format40_t *reiserfs_format40_open(aal_device_t *device, blk_t offset) {
    reiserfs_format40_t *format;
	
    if (!device)
	return NULL;
	
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    if (!(format->super = reiserfs_format40_super_open(device, offset)))
	goto error_free_format;
		
    format->device = device;
    return format;
	
error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format40_sync(reiserfs_format40_t *format) {
    blk_t offset;
    
    if (!format || !format->super)
	return -1;
   
    if (aal_device_write_block(format->device, format->super)) {
	offset = aal_device_get_block_location(format->device, format->super);
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %d.", offset);
	return -1;
    }
    return 0;
}

static reiserfs_format40_t *reiserfs_format40_create(aal_device_t *device, 
    blk_t offset, count_t blocks, uint16_t blocksize) 
{
    reiserfs_plugin_t *plugin;
    reiserfs_format40_t *format;
    reiserfs_format40_super_t *super;
//    reiserfs_segment_t request, response;

    if (!device)
	return NULL;
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;

    if (!(format->super = aal_device_alloc_block(device, offset, 0))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    super = (reiserfs_format40_super_t *)format->super->data;
    aal_memcpy(super->sb_magic, REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC));

    /* Super block forming code */
    set_sb_block_count(super, blocks);

    /* 
	Free blocks value equals blockcount minus skiped area, minus master super block,
       	minus format-specific super block, minus root internal node and minus leaf.
    */
    set_sb_free_blocks(super, blocks - ((REISERFS_MASTER_OFFSET / blocksize) + 1 + 1 + 1 + 1));

    /* Requesting allocator plugin to allocate one block for root node */
/*    if (!(plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 0x01))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find allocator plugin by its coords (%x, %x).", 
	    REISERFS_ALLOC_PLUGIN, 0x01);
	goto error_free_format;
    }

    aal_memset(&request, 0, sizeof(request));
    aal_memset(&response, 0, sizeof(response));
    
    request.start = 0;
    request.count = 1;
    
    reiserfs_plugin_check_routine(plugin->alloc, allocate, goto error_free_format);
    if (plugin->alloc.allocate(alloc, &request, &response))
	goto error_free_format;*/
    
    /* 
	Okay, allocator plugin has returned correctly filled 
	response with allocated area.
    */
    set_sb_root_block(super, offset + 1);

    /* Tree height */
    set_sb_tree_height(super, 2);

    /* 
	Smallest free oid. In theory it should be zero and then 
	it should be increased by node plugin or some item plugin.
	However, both they not accessible and we will set it into 
	"two" value (two dirs in the root dir "." and "..").
    */
    set_sb_oid(super, 2);

    /* The same as smallest oid */
    set_sb_file_count(super, 2);
    set_sb_flushes(super, 0);
    set_sb_journal_plugin_id(super, 0x01);
    set_sb_alloc_plugin_id(super, 0x01);

    return format;
    
error_free_super:
    aal_device_free_block(format->super);
error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format40_check(reiserfs_format40_t *format) {
    return reiserfs_format40_super_check((reiserfs_format40_super_t *)format->super->data, 
	format->device);
}

static void reiserfs_format40_close(reiserfs_format40_t *format) {
    aal_device_free_block(format->super);
    aal_free(format);
}

static int reiserfs_format40_probe(aal_device_t *device, blk_t offset) {
    aal_block_t *block;

    if (!(block = reiserfs_format40_super_open(device, offset)))
	return 0;
	
    aal_device_free_block(block);
    return 1;
}

static const char *formats[] = {"4.0"};

static const char *reiserfs_format40_format(reiserfs_format40_t *format) {
    return formats[0];
}

static reiserfs_plugin_id_t reiserfs_format40_journal_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format40_alloc_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static reiserfs_plugin_id_t reiserfs_format40_node_plugin(reiserfs_format40_t *format) {
    return 0x1;
}

static blk_t reiserfs_format40_root_block(reiserfs_format40_t *format) {
    return get_sb_root_block((reiserfs_format40_super_t *)format->super->data);
}

static reiserfs_plugin_t format40_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "format40",
	    .desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t))reiserfs_format40_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, blk_t, count_t, uint16_t))
	    reiserfs_format40_create,
	
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_format40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_format40_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))reiserfs_format40_check,
	.probe = (int (*)(aal_device_t *, blk_t))reiserfs_format40_probe,
	.format = (const char *(*)(reiserfs_opaque_t *))reiserfs_format40_format,
	
	.root_block = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format40_root_block,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format40_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format40_alloc_plugin,
	
	.node_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    reiserfs_format40_node_plugin,
    }
};

reiserfs_plugin_t *reiserfs_format40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &format40_plugin;
}

reiserfs_plugin_register(reiserfs_format40_entry);

