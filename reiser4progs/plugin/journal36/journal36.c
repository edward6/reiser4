/*
    journal36.c -- journal plugin for reiserfs 3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "journal36.h"

static reiserfs_core_t *core = NULL;

static errno_t journal36_header_check(reiserfs_journal36_header_t *header, 
    aal_device_t *device) 
{
    return 0;
}

static reiserfs_entity_t *journal36_open(aal_device_t *device) {
    reiserfs_journal36_t *journal;

    aal_assert("umka-406", device != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    /* Reading and checking the journal header must be here */
    
    journal->device = device;
	
    return (reiserfs_entity_t *)journal;
}

static errno_t journal36_sync(reiserfs_entity_t *entity) {
    reiserfs_journal36_t *journal;
    
    aal_assert("umka-407", entity != NULL, return -1);
    
    journal = (reiserfs_journal36_t *)entity;
    
    if (aal_block_write(journal->header)) {
	aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
	    "Can't synchronize journal header. %s.", 
	    aal_device_error(journal->device));
	return -1;
    }
    
    return 0;
}

static void journal36_close(reiserfs_entity_t *entity) {
    aal_assert("umka-408", entity != NULL, return);
    aal_free(entity);
}

static errno_t journal36_replay(reiserfs_entity_t *entity) {
    return 0;
}

static reiserfs_plugin_t journal36_plugin = {
    .journal_ops = {
	.h = {
	    .handle = NULL,
	    .id = JOURNAL_REISER36_ID,
	    .type = JOURNAL_PLUGIN_TYPE,
	    .label = "journal36",
	    .desc = "Default journal for reiserfs 3.6.x, ver. " VERSION,
	},
	.create = NULL, 
	.open	= journal36_open,
	.close	= journal36_close,
	.sync	= journal36_sync,
	.replay = journal36_replay,
	.valid	= NULL
    }
};

static reiserfs_plugin_t *journal36_start(reiserfs_core_t *c) {
    core = c;
    return &journal36_plugin;
}

libreiser4_factory_register(journal36_start);

