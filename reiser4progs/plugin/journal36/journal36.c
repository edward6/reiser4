/*
    journal36.c -- journal plugin for reiser3.6.x.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include "journal36.h"

extern reiser4_plugin_t journal36_plugin;

static reiser4_core_t *core = NULL;

static errno_t journal36_header_check(journal36_header_t *header) {
    return 0;
}

static reiser4_entity_t *journal36_open(reiser4_entity_t *format) {
    journal36_t *journal;

    aal_assert("umka-406", format != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
    
    journal->plugin = &journal36_plugin;
    
    return (reiser4_entity_t *)journal;
}

static errno_t journal36_sync(reiser4_entity_t *entity) {
    aal_assert("umka-407", entity != NULL, return -1);
    return -1;
}

static void journal36_close(reiser4_entity_t *entity) {
    aal_assert("umka-408", entity != NULL, return);
    aal_free(entity);
}

static errno_t journal36_replay(reiser4_entity_t *entity) {
    return 0;
}

static reiser4_plugin_t journal36_plugin = {
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

static reiser4_plugin_t *journal36_start(reiser4_core_t *c) {
    core = c;
    return &journal36_plugin;
}

libreiser4_factory_register(journal36_start);

