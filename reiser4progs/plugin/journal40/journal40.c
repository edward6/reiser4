/*
    journal40.c -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <aal/aal.h>
#include <reiser4/reiser4.h>

#include "journal40.h"

static reiserfs_core_t *core = NULL;

static errno_t journal40_check_header(reiserfs_journal40_header_t *header, 
    aal_device_t *device) 
{
    aal_assert("umka-515", header != NULL, return -1);
    aal_assert("umka-517", device != NULL, return -1);
    
    if (get_jh_last_commited(header) > aal_device_len(device)) {
	aal_throw_error(EO_OK, "Invalid journal header has detected. "
	    "Last commited transaction (%llu) lies out of the device (0-%llu).\n", 
	    get_jh_last_commited(header), aal_device_len(device));
	return -1;
    }
    return 0;
}

static errno_t journal40_check_footer(reiserfs_journal40_footer_t *footer, 
    aal_device_t *device) 
{
    aal_assert("umka-516", footer != NULL, return -1);
    aal_assert("umka-517", device != NULL, return -1);
    
    if (get_jf_last_flushed(footer) > aal_device_len(device)) {
	aal_throw_error(EO_OK, "Invalid journal footer has detected. "
	    "Last flushed transaction (%llu) lies out of the device (0-%llu).\n", 
	    get_jf_last_flushed(footer), aal_device_len(device));
	return -1;
    }
    return 0;
}

static reiserfs_journal40_t *journal40_open(aal_device_t *device) {
    reiserfs_journal40_t *journal;

    aal_assert("umka-409", device != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    /* Reading and chanking journal header */
    if (!(journal->header = aal_block_read(device, 
	(blk_t)(REISERFS_JOURNAL40_HEADER / aal_device_get_bs(device)))))
    {
	aal_throw_error(EO_OK, "Can't read journal header. %s.\n", 
	    aal_device_error(device));
	goto error_free_journal;
    }
	
    if (journal40_check_header(journal->header->data, device))
	goto error_free_header;
	
    /* Reading and checking journal footer */
    if (!(journal->footer = aal_block_read(device, 
	(blk_t)(REISERFS_JOURNAL40_FOOTER / aal_device_get_bs(device)))))
    {
	aal_throw_error(EO_OK, "Can't read journal footer. %s.\n", 
	    aal_device_error(device));
	goto error_free_header;
    }
	
    if (journal40_check_footer(journal->footer->data, device))
	goto error_free_footer;
    
    journal->device = device;
    return journal;

error_free_footer:
    aal_block_free(journal->footer);    
error_free_header:
    aal_block_free(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiserfs_journal40_t *journal40_create(aal_device_t *device, 
    void *params) 
{
    reiserfs_journal40_t *journal;
    
    aal_assert("umka-417", device != NULL, return NULL);
//    aal_assert("umka-418", params != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
    
    journal->device = device;

    if (!(journal->header = aal_block_alloc(device, 
	    (REISERFS_JOURNAL40_HEADER / aal_device_get_bs(device)), 0)))
	goto error_free_journal;
   
    /* Forming journal header basing on passed params */
    
    if (!(journal->footer = aal_block_alloc(device, 
	    (REISERFS_JOURNAL40_FOOTER / aal_device_get_bs(device)), 0)))
	goto error_free_header;
    
    /* Forming journal footer basing on passed params */
    
    return journal;

error_free_header:
    aal_block_free(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static errno_t journal40_sync(reiserfs_journal40_t *journal) {

    aal_assert("umka-410", journal != NULL, return -1);
    
    if (aal_block_write(journal->header)) {
	aal_throw_error(EO_OK, "Can't write journal header at %llu block. %s\n", 
	    aal_block_get_nr(journal->header), 
	    aal_device_error(journal->device));
	return -1;
    }
    
    if (aal_block_write(journal->footer)) {
	aal_throw_error(EO_OK, "Can't write journal footer at %llu block. %s.\n", 
	    aal_block_get_nr(journal->footer), 
	    aal_device_error(journal->device));
	return -1;
    }
    return 0;
}

#endif

static void journal40_close(reiserfs_journal40_t *journal) {
    aal_assert("umka-411", journal != NULL, return);

    aal_block_free(journal->header);
    aal_block_free(journal->footer);
    aal_free(journal);
}

static errno_t journal40_replay(reiserfs_journal40_t *journal) {
    aal_assert("umka-412", journal != NULL, return -1);
    return 0;
}

static void journal40_area(reiserfs_journal40_t *journal, 
    blk_t *start, blk_t *end) 
{
    aal_assert("umka-734", journal != NULL, return);
    
    *start = (REISERFS_JOURNAL40_HEADER / aal_device_get_bs(journal->device));
    *end = (REISERFS_JOURNAL40_FOOTER / aal_device_get_bs(journal->device));
}

static reiserfs_plugin_t journal40_plugin = {
    .journal_ops = {
	.h = {
	    .handle = NULL,
	    .id = REISERFS_JOURNAL40_ID,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal40",
	    .desc = "Default journal for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_entity_t *(*)(aal_device_t *))
	    journal40_open,
	
#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(aal_device_t *, void *))
	    journal40_create,

	.sync = (errno_t (*)(reiserfs_entity_t *))journal40_sync,
	.check = NULL,
#else
	.create = NULL,
	.sync = NULL,
	.check = NULL,
#endif
	.area = (void (*)(reiserfs_entity_t *, blk_t *, blk_t *))journal40_area,
	.close = (void (*)(reiserfs_entity_t *))journal40_close,
	.replay = (errno_t (*)(reiserfs_entity_t *))journal40_replay,
	.confirm = NULL,
    }
};

static reiserfs_plugin_t *journal40_entry(reiserfs_core_t *c) {
    core = c;
    return &journal40_plugin;
}

libreiser4_factory_register(journal40_entry);

