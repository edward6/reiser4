/*
	journal40.c -- reiser4 default journal plugin.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/reiserfs.h>
#include <aal/aal.h>

#include "journal40.h"

static int reiserfs_journal40_header_check(reiserfs_journal40_header_t *header, 
    aal_device_t *device) 
{
    return 1;
}

static reiserfs_journal40_t *reiserfs_journal40_open(aal_device_t *device) {
    reiserfs_journal40_t *journal;

    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    if (!(journal->header = aal_block_read(device, 
	(blk_t)(REISERFS_JOURNAL40_OFFSET / aal_device_blocksize(device)))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-026", 
	    "Can't read journal header.");
	goto error_free_journal;
    }
	
    if (!reiserfs_journal40_header_check(journal->header->data, device))
	goto error_free_header;
	
    journal->device = device;
	
    return journal;
	
error_free_header:
    aal_block_free(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static void reiserfs_journal40_close(reiserfs_journal40_t *journal, int sync) {
    if (sync && !aal_block_write(journal->device, journal->header))	{
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-027", 
	    "Can't synchronize journal header.");
    }	
    aal_free(journal);
}

static int reiserfs_journal40_replay(reiserfs_journal40_t *journal) {
    /* Journal replaying must be here. */
    return 1;
}

reiserfs_plugin_t plugin_info = {
    .journal = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal40",
	    .desc = "Default journal for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_journal_opaque_t *(*)(aal_device_t *))reiserfs_journal40_open,
	.create = NULL,
	.close = (void (*)(reiserfs_journal_opaque_t *, int))reiserfs_journal40_close,
	.replay = (int (*)(reiserfs_journal_opaque_t *))reiserfs_journal40_replay
    }
};

reiserfs_plugin_t *reiserfs_plugin_info() {
    return &plugin_info;
}

