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

static aal_block_t *reiserfs_format40_super_open(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    reiserfs_format40_super_t *super;
    
    offset = (REISERFS_FORMAT40_OFFSET / aal_device_get_blocksize(device));
	
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

static reiserfs_format40_t *reiserfs_format40_open(aal_device_t *device) {
    reiserfs_format40_t *format;

    aal_assert("umka-393", device != NULL, return NULL);
	
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

static error_t reiserfs_format40_sync(reiserfs_format40_t *format) {
    blk_t offset;
   
    aal_assert("umka-394", format != NULL, return -1); 
   
    if (aal_device_write_block(format->device, format->super)) {
	offset = aal_device_get_block_location(format->device, format->super);
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %d.", offset);
	return -1;
    }
    return 0;
}

static reiserfs_format40_t *reiserfs_format40_create(aal_device_t *device, 
    count_t blocks, reiserfs_opaque_t *alloc)
{
    blk_t blk;
    reiserfs_plugin_t *plugin;
    reiserfs_format40_t *format;
    reiserfs_format40_super_t *super;

    aal_assert("umka-395", device != NULL, return NULL);
    aal_assert("umka-396", alloc != NULL, return NULL);
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = device;

    if (!(format->super = aal_device_alloc_block(device, 
	(REISERFS_FORMAT40_OFFSET / aal_device_get_blocksize(device)), 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    super = (reiserfs_format40_super_t *)format->super->data;
    aal_memcpy(super->sb_magic, REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC));

    /* Super block forming code */
    set_sb_block_count(super, blocks);

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

    if (!(plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 0x1))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find allocator plugin by its id %x.", 0x1);
	goto error_free_format;
    }

    reiserfs_plugin_check_routine(plugin->alloc, use, goto error_free_format);
    
    /* Marking the skiped area (0-16 blocks) as used */
    for (blk = 0; blk < (blk_t)(REISERFS_MASTER_OFFSET / aal_device_get_blocksize(device)); blk++)
	plugin->alloc.use(alloc, blk);
    
    /* Marking master super block as used */
    plugin->alloc.use(alloc, (REISERFS_MASTER_OFFSET / aal_device_get_blocksize(device)));
    
    /* Marking format-specific super block as used */
    plugin->alloc.use(alloc, (REISERFS_FORMAT40_OFFSET / aal_device_get_blocksize(device)));
    
    return format;
    
error_free_super:
    aal_device_free_block(format->super);
error_free_format:
    aal_free(format);
error:
    return NULL;
}

static error_t reiserfs_format40_check(reiserfs_format40_t *format) {
    aal_assert("umka-397", format != NULL, return -1);
    return reiserfs_format40_super_check((reiserfs_format40_super_t *)format->super->data, 
	format->device);
}

static void reiserfs_format40_close(reiserfs_format40_t *format) {
    aal_assert("umka-398", format != NULL, return);
    
    aal_device_free_block(format->super);
    aal_free(format);
}

static int reiserfs_format40_probe(aal_device_t *device) {
    aal_block_t *block;

    if (!(block = reiserfs_format40_super_open(device)))
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

static blk_t reiserfs_format40_offset(reiserfs_format40_t *format) {
    aal_assert("umka-399", format != NULL, return 0);
    return (REISERFS_FORMAT40_OFFSET / aal_device_get_blocksize(format->device));
}

static blk_t reiserfs_format40_get_root(reiserfs_format40_t *format) {
    aal_assert("umka-400", format != NULL, return 0);
    return get_sb_root_block((reiserfs_format40_super_t *)format->super->data);
}

static count_t reiserfs_format40_get_blocks(reiserfs_format40_t *format) {
    aal_assert("umka-401", format != NULL, return 0);
    return get_sb_block_count((reiserfs_format40_super_t *)format->super->data);
}

static count_t reiserfs_format40_get_free(reiserfs_format40_t *format) {
    aal_assert("umka-402", format != NULL, return 0);
    return get_sb_free_blocks((reiserfs_format40_super_t *)format->super->data);
}

static void reiserfs_format40_set_root(reiserfs_format40_t *format, blk_t root) {
    aal_assert("umka-403", format != NULL, return);
    set_sb_root_block((reiserfs_format40_super_t *)format->super->data, root);
}

static void reiserfs_format40_set_blocks(reiserfs_format40_t *format, count_t blocks) {
    aal_assert("umka-404", format != NULL, return);
    set_sb_block_count((reiserfs_format40_super_t *)format->super->data, blocks);
}

static void reiserfs_format40_set_free(reiserfs_format40_t *format, count_t blocks) {
    aal_assert("umka-405", format != NULL, return);
    set_sb_free_blocks((reiserfs_format40_super_t *)format->super->data, blocks);
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
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))reiserfs_format40_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t, reiserfs_opaque_t *))
	    reiserfs_format40_create,
	
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_format40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_format40_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))reiserfs_format40_check,
	.probe = (int (*)(aal_device_t *))reiserfs_format40_probe,
	.format = (const char *(*)(reiserfs_opaque_t *))reiserfs_format40_format,
	
	.offset = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format40_offset,
	
	.get_root = (blk_t (*)(reiserfs_opaque_t *))reiserfs_format40_get_root,
	.get_blocks = (count_t (*)(reiserfs_opaque_t *))reiserfs_format40_get_blocks,
	.get_free = (count_t (*)(reiserfs_opaque_t *))reiserfs_format40_get_free,
	
	.set_root = (void (*)(reiserfs_opaque_t *, blk_t))reiserfs_format40_set_root,
	.set_blocks = (void (*)(reiserfs_opaque_t *, count_t))reiserfs_format40_set_blocks,
	.set_free = (void (*)(reiserfs_opaque_t *, count_t))reiserfs_format40_set_free,
	
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

