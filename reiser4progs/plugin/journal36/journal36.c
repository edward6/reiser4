/*
    journal36.c -- journal plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "journal36.h"

static reiserfs_plugin_factory_t *factory = NULL;

static error_t journal36_header_check(journal36_header_t *header, 
    aal_device_t *device) 
{
    return 0;
}

static journal36_t *journal36_open(aal_device_t *device) {
    journal36_t *journal;

    aal_assert("umka-406", device != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    /* Reading and checking the journal header must be here */
    
    journal->device = device;
	
    return journal;
}

static error_t journal36_sync(journal36_t *journal) {
    
    aal_assert("umka-407", journal != NULL, return -1);
    
    if (aal_block_write(journal->device, journal->header)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't synchronize journal header.");
	return -1;
    }
    return 0;
}

static void journal36_close(journal36_t *journal) {
    aal_assert("umka-408", journal != NULL, return);
    
    aal_free(journal);
}

static error_t journal36_replay(journal36_t *journal) {
    return 0;
}

static reiserfs_plugin_t journal36_plugin = {
    .journal = {
	.h = {
	    .handle = NULL,
	    .id = 0x1,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal36",
	    .desc = "Default journal for reiserfs 3.6.x, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))journal36_open,
	.create = NULL, 
	.close = (void (*)(reiserfs_opaque_t *))journal36_close,
	.sync = (error_t (*)(reiserfs_opaque_t *))journal36_sync,
	.replay = (error_t (*)(reiserfs_opaque_t *))journal36_replay
    }
};

reiserfs_plugin_t *journal36_entry(reiserfs_plugin_factory_t *f) {
    factory = f;
    return &journal36_plugin;
}

libreiserfs_plugins_register(journal36_entry);

