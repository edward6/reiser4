/*
    journal36.c -- journal plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "journal36.h"

static error_t reiserfs_journal36_header_check(reiserfs_journal36_header_t *header, 
    aal_device_t *device) 
{
    return 0;
}

static reiserfs_journal36_t *reiserfs_journal36_open(aal_device_t *device) {
    reiserfs_journal36_t *journal;

    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    /* Reading and checking the journal header must be here */
    
    journal->device = device;
	
    return journal;
}

static error_t reiserfs_journal36_sync(reiserfs_journal36_t *journal) {
    if (!aal_device_write_block(journal->device, journal->header)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't synchronize journal header.");
	return -1;
    }
    return 0;
}

static void reiserfs_journal36_close(reiserfs_journal36_t *journal, int sync) {
    if (sync)
	reiserfs_journal36_sync(journal);
    
    aal_free(journal);
}

static error_t reiserfs_journal36_replay(reiserfs_journal36_t *journal) {
    /* Journal replaying must be here. */
    return 0;
}

static reiserfs_plugin_t journal36_plugin = {
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
	.sync = (error_t (*)(reiserfs_journal_opaque_t *))reiserfs_journal36_sync,
	.replay = (error_t (*)(reiserfs_journal_opaque_t *))reiserfs_journal36_replay
    }
};

reiserfs_plugin_register(journal36_plugin);

