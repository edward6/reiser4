/*
    journal40.c -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "journal40.h"

extern reiser4_plugin_t journal40_plugin;

static reiser4_core_t *core = NULL;

static errno_t journal40_hcheck(journal40_header_t *header) {
    aal_assert("umka-515", header != NULL, return -1);
    return 0;
}

static errno_t journal40_fcheck(journal40_footer_t *footer) {
    aal_assert("umka-516", footer != NULL, return -1);
    return 0;
}

static errno_t callback_fetch_journal(aal_device_t *device, 
    blk_t blk, void *data)
{
    journal40_t *journal = (journal40_t *)data;

    if (!journal->header) {
	if (!(journal->header = aal_block_open(device, blk))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't read journal header from block %llu. %s.", 
		blk, device->error);
	    return -1;
	}
    } else {
	if (!(journal->footer = aal_block_open(device, blk))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't read journal footer from block %llu. %s.", 
		blk, device->error);
	    return -1;
	}
    }
    
    return 0;
}

static reiser4_entity_t *journal40_open(reiser4_entity_t *format) {
    journal40_t *journal;
    reiser4_layout_func_t layout;

    aal_assert("umka-409", format != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    journal->format = format;
    
    if (!(layout = format->plugin->format_ops.journal_layout)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Method \"journal_layout\" doesn't implemented in format plugin.");
	goto error_free_journal;
    }
    
    if (layout(format, callback_fetch_journal, journal)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't load journal metadata.");
	goto error_free_journal;
    }
    
    journal->plugin = &journal40_plugin;

    return (reiser4_entity_t *)journal;

error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static errno_t journal40_valid(reiser4_entity_t *entity) {
    journal40_t *journal = (journal40_t *)entity;
    
    aal_assert("umka-965", journal != NULL, return -1);
    
    if (journal40_hcheck(journal->header->data))
	return -1;
	
    if (journal40_fcheck(journal->footer->data))
	return -1;
    
    return 0;
}

#ifndef ENABLE_COMPACT

static errno_t callback_alloc_journal(aal_device_t *device, 
    blk_t blk, void *data)
{
    journal40_t *journal = (journal40_t *)data;

    if (!journal->header) {
	if (!(journal->header = aal_block_create(device, blk, 0))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't alloc journal header on block %llu.", blk);
	    return -1;
	}
    } else {
	if (!(journal->footer = aal_block_create(device, blk, 0))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't alloc journal footer on block %llu.", blk);
	    return -1;
	}
    }
    
    return 0;
}

static reiser4_entity_t *journal40_create(reiser4_entity_t *format,
    void *params) 
{
    journal40_t *journal;
    reiser4_layout_func_t layout;
    
    aal_assert("umka-1057", format != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
    
    journal->format = format;
    
    if (!(layout = format->plugin->format_ops.journal_layout)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Method \"journal_layout\" doesn't implemented in format plugin.");
	goto error_free_journal;
    }
    
    if (layout(format, callback_alloc_journal, journal)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't load journal metadata.");
	goto error_free_journal;
    }
    
    journal->plugin = &journal40_plugin;
    
    return (reiser4_entity_t *)journal;

error_free_header:
    aal_block_free(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static errno_t callback_flush_journal(aal_device_t *device, 
    blk_t blk, void *data)
{
    journal40_t *journal = (journal40_t *)data;

    if (blk == aal_block_number(journal->header)) {
	if (aal_block_sync(journal->header)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't write journal header to block %llu. %s.", 
		blk, device->error);
	    return -1;
	}
    } else {
	if (aal_block_sync(journal->footer)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't write journal footer to block %llu. %s.", 
		blk, device->error);
	    return -1;
	}
    }
    
    return 0;
}
static errno_t journal40_sync(reiser4_entity_t *entity) {
    reiser4_layout_func_t layout;
    journal40_t *journal = (journal40_t *)entity;

    aal_assert("umka-410", journal != NULL, return -1);
    
    if (!(layout = journal->format->plugin->format_ops.journal_layout)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Method \"journal_layout\" doesn't implemented in format plugin.");
	return -1;
    }
    
    if (layout(journal->format, callback_flush_journal, journal)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't load journal metadata.");
	return -1;
    }
    
    return 0;
}

#endif

static void journal40_close(reiser4_entity_t *entity) {
    journal40_t *journal = (journal40_t *)entity;
    
    aal_assert("umka-411", entity != NULL, return);

    aal_block_free(journal->header);
    aal_block_free(journal->footer);
    aal_free(journal);
}

static errno_t journal40_replay(reiser4_entity_t *entity) {
    aal_assert("umka-412", entity != NULL, return -1);
    return 0;
}

static reiser4_plugin_t journal40_plugin = {
    .journal_ops = {
	.h = {
	    .handle = NULL,
	    .id = JOURNAL_REISER40_ID,
	    .type = JOURNAL_PLUGIN_TYPE,
	    .label = "journal40",
	    .desc = "Default journal for reiserfs 4.0, ver. " VERSION,
	},
	.open	= journal40_open,
	
#ifndef ENABLE_COMPACT
	.create	= journal40_create,
	.sync	= journal40_sync,
#else
	.create = NULL,
	.sync	= NULL,
#endif
	.valid	= journal40_valid,
	.close	= journal40_close,
	.replay = journal40_replay,
    }
};

static reiser4_plugin_t *journal40_start(reiser4_core_t *c) {
    core = c;
    return &journal40_plugin;
}

libreiser4_factory_register(journal40_start);

