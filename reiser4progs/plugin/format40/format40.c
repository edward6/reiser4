/*
    format40.c -- default disk-layout plugin for reiserfs 4.0.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "format40.h"

static reiserfs_plugin_factory_t *factory = NULL;

static error_t format40_super_check(format40_super_t *super, 
    aal_device_t *device) 
{
    blk_t offset;
    blk_t dev_len = aal_device_len(device);
    
    if (get_sb_block_count(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_CANCEL,
	    "Superblock has an invalid block count %llu for device "
	    "length %llu blocks.", get_sb_block_count(super), dev_len);
	return -1;
    }
    
    offset = (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(device));
    if (get_sb_root_block(super) < offset || get_sb_root_block(super) > dev_len) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Superblock has an invalid root block %llu for device "
	    "length %llu blocks.", get_sb_root_block(super), dev_len);
	return -1;
    }
    return 0;
}

static int format40_signature(format40_super_t *super) {
    return aal_strncmp(super->sb_magic, 
	REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC)) == 0;
}

static aal_block_t *format40_super_open(aal_device_t *device) {
    blk_t offset;
    aal_block_t *block;
    format40_super_t *super;
    
    offset = (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(device));
	
    if (!(block = aal_block_read(device, offset))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't read block %llu.", offset);
	return NULL;
    }
    super = (format40_super_t *)block->data;
    
    if (!format40_signature(super))
	return NULL;
    
    if (format40_super_check(super, device)) {
        aal_block_free(block);
        return NULL;
    }
    
    return block;
}

/* This function should find most recent copy of the super block */
static format40_t *format40_open(aal_device_t *host_device, 
    aal_device_t *journal_device) 
{
    format40_t *format;
    reiserfs_plugin_t *journal_plugin;
    reiserfs_plugin_t *alloc_plugin;
    reiserfs_plugin_t *oid_plugin;

    aal_assert("umka-393", host_device != NULL, return NULL);

    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;

    format->device = host_device;
    
    if (!(format->super = format40_super_open(host_device)))
	goto error_free_format;
		
    if (!(alloc_plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 
	REISERFS_FORMAT40_ALLOC))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_ALLOC);
	goto error_free_super;
    }
    
    if (!(format->alloc = libreiserfs_plugins_call(goto error_free_super, 
	alloc_plugin->alloc, open, host_device, 
	get_sb_block_count((format40_super_t *)format->super->data)))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open allocator \"%s\".", alloc_plugin->h.label);
	goto error_free_super;
    }

    if (journal_device) {
	if (!(journal_plugin = factory->find_by_coords(REISERFS_JOURNAL_PLUGIN, 
	    REISERFS_FORMAT40_JOURNAL))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't find journal plugin by its id %x.", 
		REISERFS_FORMAT40_JOURNAL);
	    goto error_free_alloc;
	}
    
	if (!(format->journal = libreiserfs_plugins_call(goto error_free_alloc, 
	    journal_plugin->journal, open, journal_device))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't open journal \"%s\".", journal_plugin->h.label);
	    goto error_free_alloc;
	}
    }
    
    if (!(oid_plugin = factory->find_by_coords(REISERFS_OID_PLUGIN, 
	REISERFS_FORMAT40_OID))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find oid allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_OID);
	goto error_free_journal;
    }
    
    /* Initializing oid allocator on super block */
    if (!(format->oid = libreiserfs_plugins_call(goto error_free_journal, 
	oid_plugin->oid, open, &((format40_super_t *)format->super->data)->sb_oid, 2))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open oid allocator \"%s\".", oid_plugin->h.label);
	goto error_free_journal;
    }
    
    return format;

error_free_journal:
    if (format->journal) {
	libreiserfs_plugins_call(goto error_free_alloc, 
	    journal_plugin->journal, close, format->journal);
    }
error_free_alloc:
    libreiserfs_plugins_call(goto error_free_super, 
	alloc_plugin->alloc, close, format->alloc);
error_free_super:
    aal_block_free(format->super);
error_free_format:
    aal_free(format);
error:
    return NULL;
}

/* This function should create super block and update all copies */
static format40_t *format40_create(aal_device_t *host_device, 
    count_t blocks, aal_device_t *journal_device, reiserfs_params_opaque_t *journal_params)
{
    blk_t blk;
    format40_t *format;
    format40_super_t *super;
    
    reiserfs_plugin_t *journal_plugin;
    reiserfs_plugin_t *alloc_plugin;
    reiserfs_plugin_t *oid_plugin;

    aal_assert("umka-395", host_device != NULL, return NULL);
    
    if (!(format = aal_calloc(sizeof(*format), 0)))
	return NULL;
    
    format->device = host_device;
    if (!journal_device)
	journal_device = host_device;

    if (!(format->super = aal_block_alloc(host_device, 
	(REISERFS_FORMAT40_OFFSET / aal_device_get_bs(host_device)), 0))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't allocate superblock.");
	goto error_free_format;
    }
    super = (format40_super_t *)format->super->data;
    aal_memcpy(super->sb_magic, REISERFS_FORMAT40_MAGIC, aal_strlen(REISERFS_FORMAT40_MAGIC));

    /* Super block forming code */
    set_sb_block_count(super, blocks);

    /* Tree height */
    set_sb_tree_height(super, 2);

    /* The same as smallest oid */
    set_sb_file_count(super, 2);
    set_sb_oid(super, REISERFS_FORMAT40_OID_RESERVED);
    
    set_sb_flushes(super, 0);

    if (!(alloc_plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 
	REISERFS_FORMAT40_ALLOC))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find allocator plugin by its id %x.", REISERFS_FORMAT40_ALLOC);
	goto error_free_super;
    }
    
    if (!(format->alloc = libreiserfs_plugins_call(goto error_free_super, 
	alloc_plugin->alloc, create, host_device, blocks))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create allocator \"%s\".", alloc_plugin->h.label);
	goto error_free_super;
    }
    
    /* Marking the skiped area (0-16 blocks) as used */
    for (blk = 0; blk < (blk_t)(REISERFS_MASTER_OFFSET / 
	    aal_device_get_bs(host_device)); blk++)
    {
	libreiserfs_plugins_call(goto error_free_alloc, alloc_plugin->alloc, 
	    mark, format->alloc, blk);
    }
    
    /* Marking master super block as used */
    libreiserfs_plugins_call(goto error_free_alloc, alloc_plugin->alloc, 
	mark, format->alloc, (REISERFS_MASTER_OFFSET / aal_device_get_bs(host_device)));
    
    /* Marking format-specific super block as used */
    libreiserfs_plugins_call(goto error_free_alloc, alloc_plugin->alloc, 
	mark, format->alloc, (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(host_device)));
    
    if (!(journal_plugin = factory->find_by_coords(REISERFS_JOURNAL_PLUGIN, 
	REISERFS_FORMAT40_JOURNAL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find journal plugin by its id %x.", REISERFS_FORMAT40_JOURNAL);
	goto error_free_alloc;
    }
    
    if (!(format->journal = libreiserfs_plugins_call(goto error_free_alloc, 
	journal_plugin->journal, create, journal_device, journal_params))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create journal \"%s\".", journal_plugin->h.label);
	goto error_free_alloc;
    }

    /* Marking journal blocks as used */
    libreiserfs_plugins_call(goto error_free_alloc, alloc_plugin->alloc, 
	mark, format->alloc, (REISERFS_FORMAT40_JOURNAL_HEADER / 
	aal_device_get_bs(host_device)));
    
    libreiserfs_plugins_call(goto error_free_alloc, alloc_plugin->alloc, 
	mark, format->alloc, (REISERFS_FORMAT40_JOURNAL_FOOTER / 
	aal_device_get_bs(host_device)));
    
    if (!(oid_plugin = factory->find_by_coords(REISERFS_OID_PLUGIN, 
	REISERFS_FORMAT40_OID))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find oid plugin by its id %x.", REISERFS_FORMAT40_OID);
	goto error_free_journal;
    }
    
    if (!(format->oid = libreiserfs_plugins_call(goto error_free_journal, 
	oid_plugin->oid, open, &((format40_super_t *)format->super->data)->sb_oid, 2))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open oid allocator \"%s\".", oid_plugin->h.label);
	goto error_free_journal;
    }
    
    set_sb_oid(super, libreiserfs_plugins_call(goto error_free_oid, oid_plugin->oid, 
	next, format->oid));
    
    return format;

error_free_oid:
    libreiserfs_plugins_call(goto error_free_journal, oid_plugin->oid, 
	close, format->oid);
error_free_journal:
    libreiserfs_plugins_call(goto error_free_journal, journal_plugin->journal, 
	close, format->journal);
error_free_alloc:
    libreiserfs_plugins_call(goto error_free_super, alloc_plugin->alloc, 
	close, format->alloc);
error_free_super:
    aal_block_free(format->super);
error_free_format:
    aal_free(format);
error:
    return NULL;
}

/* This function should update all copies of the super block */
static error_t format40_sync(format40_t *format) {
    blk_t offset;
    reiserfs_plugin_t *plugin;
   
    aal_assert("umka-394", format != NULL, return -1); 
   
    if (!(plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 
	REISERFS_FORMAT40_ALLOC))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_ALLOC);
	return -1;
    }
    
    libreiserfs_plugins_call(return -1, plugin->alloc, sync, 
	format->alloc);
    
    if (!(plugin = factory->find_by_coords(REISERFS_JOURNAL_PLUGIN, 
	REISERFS_FORMAT40_JOURNAL))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find journal plugin by its id %x.", 
	    REISERFS_FORMAT40_JOURNAL);
	return -1;
    }
    
    libreiserfs_plugins_call(return -1, plugin->journal, sync, 
	format->journal);
    
    if (!(plugin = factory->find_by_coords(REISERFS_OID_PLUGIN, 
	REISERFS_FORMAT40_OID))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find oid allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_OID);
	return -1;
    }
    
    set_sb_oid((format40_super_t *)format->super->data, 
	libreiserfs_plugins_call(return -1, plugin->oid, next, format->oid));
    
    set_sb_file_count((format40_super_t *)format->super->data, 
	libreiserfs_plugins_call(return -1, plugin->oid, used, format->oid));
    
    if (aal_block_write(format->device, format->super)) {
	offset = aal_block_get_nr(format->super);
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't write superblock to %llu.", offset);
	return -1;
    }
    
    return 0;
}

static error_t format40_check(format40_t *format) {
    aal_assert("umka-397", format != NULL, return -1);
    
    return format40_super_check((format40_super_t *)format->super->data, 
	format->device);
}

static void format40_close(format40_t *format) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-398", format != NULL, return);
    
    if (!(plugin = factory->find_by_coords(REISERFS_ALLOC_PLUGIN, 
	REISERFS_FORMAT40_ALLOC))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_ALLOC);
    }
    
    libreiserfs_plugins_call(goto error_free_journal, plugin->alloc, 
	close, format->alloc);

error_free_journal:
    if (format->journal) {
	if (!(plugin = factory->find_by_coords(REISERFS_JOURNAL_PLUGIN, 
	    REISERFS_FORMAT40_JOURNAL))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
		"Can't find journal plugin by its id %x.", 
		REISERFS_FORMAT40_JOURNAL);
	}
    
	libreiserfs_plugins_call(goto error_free_oid, plugin->journal, 
	    close, format->journal);
    }

error_free_oid:
    if (!(plugin = factory->find_by_coords(REISERFS_OID_PLUGIN, 
	REISERFS_FORMAT40_OID))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't find oid allocator plugin by its id %x.", 
	    REISERFS_FORMAT40_OID);
    }
    
    libreiserfs_plugins_call(goto error_free_super, plugin->journal, 
	close, format->oid);

error_free_super:
    aal_block_free(format->super);
    aal_free(format);
}

static int format40_confirm(aal_device_t *device) {
    aal_block_t *block;

    if (!(block = format40_super_open(device)))
	return 0;
	
    aal_block_free(block);
    return 1;
}

static const char *formats[] = {"4.0"};

static const char *format40_format(format40_t *format) {
    return formats[0];
}

static reiserfs_plugin_id_t format40_journal_plugin(format40_t *format) {
    return REISERFS_FORMAT40_JOURNAL;
}

static reiserfs_plugin_id_t format40_alloc_plugin(format40_t *format) {
    return REISERFS_FORMAT40_ALLOC;
}

static reiserfs_plugin_id_t format40_oid_plugin(format40_t *format) {
    return REISERFS_FORMAT40_OID;
}

static blk_t format40_offset(format40_t *format) {
    aal_assert("umka-399", format != NULL, return 0);
    return (REISERFS_FORMAT40_OFFSET / aal_device_get_bs(format->device));
}

static reiserfs_opaque_t *format40_journal(format40_t *format) {
    aal_assert("umka-489", format != NULL, return 0);
    return format->journal;
}

static reiserfs_opaque_t *format40_alloc(format40_t *format) {
    aal_assert("umka-508", format != NULL, return 0);
    return format->alloc;
}

static reiserfs_opaque_t *format40_oid(format40_t *format) {
    aal_assert("umka-509", format != NULL, return 0);
    return format->oid;
}

static blk_t format40_get_root(format40_t *format) {
    aal_assert("umka-400", format != NULL, return 0);
    return get_sb_root_block((format40_super_t *)format->super->data);
}

static count_t format40_get_blocks(format40_t *format) {
    aal_assert("umka-401", format != NULL, return 0);
    return get_sb_block_count((format40_super_t *)format->super->data);
}

static count_t format40_get_free(format40_t *format) {
    aal_assert("umka-402", format != NULL, return 0);
    return get_sb_free_blocks((format40_super_t *)format->super->data);
}

static uint16_t format40_get_height(format40_t *format) {
    aal_assert("umka-555", format != NULL, return 0);
    return get_sb_tree_height((format40_super_t *)format->super->data);
}

static void format40_set_root(format40_t *format, blk_t root) {
    aal_assert("umka-403", format != NULL, return);
    set_sb_root_block((format40_super_t *)format->super->data, root);
}

static void format40_set_blocks(format40_t *format, count_t blocks) {
    aal_assert("umka-404", format != NULL, return);
    set_sb_block_count((format40_super_t *)format->super->data, blocks);
}

static void format40_set_free(format40_t *format, count_t blocks) {
    aal_assert("umka-405", format != NULL, return);
    set_sb_free_blocks((format40_super_t *)format->super->data, blocks);
}

static void format40_set_height(format40_t *format, uint16_t height) {
    aal_assert("umka-555", format != NULL, return);
    set_sb_tree_height((format40_super_t *)format->super->data, height);
}

static reiserfs_plugin_t format40_plugin = {
    .format = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_FORMAT_PLUGIN,
	    .label = "format40",
	    .desc = "Disk-layout for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *, aal_device_t *))
	    format40_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, count_t, 
	    aal_device_t *, reiserfs_params_opaque_t *))format40_create,

	.close = (void (*)(reiserfs_opaque_t *))format40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))format40_sync,
	.check = (error_t (*)(reiserfs_opaque_t *))format40_check,
	.confirm = (int (*)(aal_device_t *))format40_confirm,
	.format = (const char *(*)(reiserfs_opaque_t *))format40_format,
	
	.offset = (blk_t (*)(reiserfs_opaque_t *))format40_offset,
	
	.get_root = (blk_t (*)(reiserfs_opaque_t *))format40_get_root,
	.set_root = (void (*)(reiserfs_opaque_t *, blk_t))format40_set_root,
	
	.get_blocks = (count_t (*)(reiserfs_opaque_t *))format40_get_blocks,
	.set_blocks = (void (*)(reiserfs_opaque_t *, count_t))format40_set_blocks,
	
	.get_free = (count_t (*)(reiserfs_opaque_t *))format40_get_free,
	.set_free = (void (*)(reiserfs_opaque_t *, count_t))format40_set_free,
	
	.get_height = (uint16_t (*)(reiserfs_opaque_t *))format40_get_height,
	.set_height = (void (*)(reiserfs_opaque_t *, uint16_t))format40_set_height,
	
	.journal_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format40_journal_plugin,
		
	.alloc_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format40_alloc_plugin,
	
	.oid_plugin_id = (reiserfs_plugin_id_t(*)(reiserfs_opaque_t *))
	    format40_oid_plugin,
	
	.journal = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format40_journal,
	.alloc = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format40_alloc,
	.oid = (reiserfs_opaque_t *(*)(reiserfs_opaque_t *))format40_oid,
    }
};

reiserfs_plugin_t *format40_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &format40_plugin;
}

libreiserfs_plugins_register(format40_entry);

