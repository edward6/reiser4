/*
    journal36.c -- journal plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>
#include <aal/aal.h>

#include "journal36.h"

static int reiserfs_journal36_header_check(reiserfs_journal36_header_t *header, 
    aal_device_t *device) 
{
    return 1;
}

static reiserfs_journal36_t *reiserfs_journal36_open(aal_device_t *device) {
    reiserfs_journal36_t *journal;

    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    /* Reading and checking the journal header must be here */
    
    journal->device = device;
	
    return journal;
}

static void reiserfs_journal36_close(reiserfs_journal36_t *journal, int sync) {
    if (sync && !aal_block_write(journal->device, journal->header))	{
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-025", 
	    "Can't synchronize journal header.");
    }	
    aal_free(journal);
}

static int reiserfs_journal36_replay(reiserfs_journal36_t *journal) {
    /* Journal replaying must be here. */
    return 1;
}

reiserfs_plugin_t plugin_info = {
    .journal = {
	.h = {
	    .handle = NULL,
	    .id = 0x2,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal36",
	    .desc = "Default journal for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_journal_opaque_t *(*)(aal_device_t *))reiserfs_journal36_open,
	.create = NULL, 
	.close = (void (*)(reiserfs_journal_opaque_t *, int))reiserfs_journal36_close,
	.replay = (int (*)(reiserfs_journal_opaque_t *))reiserfs_journal36_replay
    }
};

reiserfs_plugin_t *reiserfs_plugin_info() {
    return &plugin_info;
}

