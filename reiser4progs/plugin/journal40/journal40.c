/*
    journal40.c -- reiser4 default journal plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>

#include "journal40.h"

static reiserfs_plugins_factory_t *factory = NULL;

static error_t reiserfs_journal40_check_header(reiserfs_journal40_header_t *header, 
    aal_device_t *device) 
{
    aal_assert("umka-515", header != NULL, return -1);
    aal_assert("umka-517", device != NULL, return -1);
    
    if (get_jh_last_commited(header) > aal_device_len(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Invalid journal header has detected. "
	    "Last commited transaction (%llu) lies out of the device (0-%llu).", 
	    get_jh_last_commited(header), aal_device_len(device));
	return -1;
    }
    return 0;
}

static error_t reiserfs_journal40_check_footer(reiserfs_journal40_footer_t *footer, 
    aal_device_t *device) 
{
    aal_assert("umka-516", footer != NULL, return -1);
    aal_assert("umka-517", device != NULL, return -1);
    
    if (get_jf_last_flushed(footer) > aal_device_len(device)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Invalid journal footer has detected. "
	    "Last flushed transaction (%llu) lies out of the device (0-%llu).", 
	    get_jf_last_flushed(footer), aal_device_len(device));
	return -1;
    }
    return 0;
}

static reiserfs_journal40_t *reiserfs_journal40_open(aal_device_t *device) {
    reiserfs_journal40_t *journal;

    aal_assert("umka-409", device != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    /* Reading and chanking journal header */
    if (!(journal->header = aal_device_read_block(device, 
	(blk_t)(REISERFS_JOURNAL40_HEADER / aal_device_get_blocksize(device)))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read journal header.");
	goto error_free_journal;
    }
	
    if (!reiserfs_journal40_check_header(journal->header->data, device))
	goto error_free_header;
	
    /* Reading and checking journal footer */
    if (!(journal->footer = aal_device_read_block(device, 
	(blk_t)(REISERFS_JOURNAL40_FOOTER / aal_device_get_blocksize(device)))))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't read journal footer.");
	goto error_free_header;
    }
	
    if (!reiserfs_journal40_check_footer(journal->footer->data, device))
	goto error_free_footer;
    
    journal->device = device;
	
    return journal;

error_free_footer:
    aal_device_free_block(journal->footer);    
error_free_header:
    aal_device_free_block(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static reiserfs_journal40_t *reiserfs_journal40_create(aal_device_t *device, 
    reiserfs_params_opaque_t *params) 
{
    reiserfs_journal40_t *journal;
    
    aal_assert("umka-417", device != NULL, return NULL);
//    aal_assert("umka-418", params != NULL, return NULL);
    
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
    
    journal->device = device;

    if (!(journal->header = aal_device_alloc_block(device, 
	    (REISERFS_JOURNAL40_HEADER / aal_device_get_blocksize(device)), 0)))
	goto error_free_journal;
   
    /* Forming journal header basing on passed params */
    
    if (!(journal->footer = aal_device_alloc_block(device, 
	    (REISERFS_JOURNAL40_FOOTER / aal_device_get_blocksize(device)), 0)))
	goto error_free_header;
    
    /* Forming journal footer basing on passed params */
    
    return journal;

error_free_header:
    aal_device_free_block(journal->header);
error_free_journal:
    aal_free(journal);
error:
    return NULL;
}

static error_t reiserfs_journal40_sync(reiserfs_journal40_t *journal) {

    aal_assert("umka-410", journal != NULL, return -1);
    
    if (aal_device_write_block(journal->device, journal->header)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write journal header at %llu block.", 
	    aal_device_get_block_nr(journal->device, journal->header));
	return -1;
    }
    
    if (aal_device_write_block(journal->device, journal->footer)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't write journal footer at %llu block.", 
	    aal_device_get_block_nr(journal->device, journal->footer));
	return -1;
    }
    return 0;
}

static void reiserfs_journal40_close(reiserfs_journal40_t *journal) {
    aal_assert("umka-411", journal != NULL, return);

    aal_device_free_block(journal->header);
    aal_device_free_block(journal->footer);
    aal_free(journal);
}

static error_t reiserfs_journal40_replay(reiserfs_journal40_t *journal) {
    aal_assert("umka-412", journal != NULL, return -1);
    return 0;
}

static reiserfs_plugin_t journal40_plugin = {
    .journal = {
	.h = {
	    .handle = NULL,
	    .id = 0x0,
	    .type = REISERFS_JOURNAL_PLUGIN,
	    .label = "journal40",
	    .desc = "Default journal for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.open = (reiserfs_opaque_t *(*)(aal_device_t *))reiserfs_journal40_open,
	
	.create = (reiserfs_opaque_t *(*)(aal_device_t *, reiserfs_params_opaque_t *))
	    reiserfs_journal40_create,
	
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

