/*
    journal40.c -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>
#include <aal/aal.h>

#include "journal40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_journal40_header_check(reiserfs_journal40_header_t *header, 
    aal_device_t *device) 
{
    return 0;
}

static reiserfs_journal40_t *reiserfs_journal40_open(aal_device_t *device) {
    reiserfs_journal40_t *journal;

    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    if (!(journal->header = aal_device_read_block(device, 
	(blk_t)(REISERFS_JOURNAL40_OFFSET / aal_device_get_blocksize(device)))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read journal header.");
	goto error_free_journal;
    }
	
    if (!reiserfs_journal40_header_check(journal->header->data, device))
	goto error_free_header;
	
    journal->device = device;
	
    return journal;
	
error_free_header:
    aal_device_free_block(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static error_t reiserfs_journal40_sync(reiserfs_journal40_t *journal) {
    if (aal_device_write_block(journal->device, journal->header)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't synchronize journal header.");
	return -1;
    }
    return 0;
}

static void reiserfs_journal40_close(reiserfs_journal40_t *journal) {
    aal_free(journal);
}

static error_t reiserfs_journal40_replay(reiserfs_journal40_t *journal) {
    /* Journal replaying must be here. */
    return 0;
}

static reiserfs_plugin_t journal40_plugin = {
    .journal = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal40",
	    .desc = "Default journal for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))reiserfs_journal40_open,
	.create = NULL,
	.close = (void (*)(reiserfs_opaque_t *))reiserfs_journal40_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))reiserfs_journal40_sync,
	.replay = (error_t (*)(reiserfs_opaque_t *))reiserfs_journal40_replay
    }
};

reiserfs_plugin_t *reiserfs_journal40_entry(reiserfs_plugins_factory_t *f) {
    factory = f;
    return &journal40_plugin;
}

reiserfs_plugin_register(reiserfs_journal40_entry);

