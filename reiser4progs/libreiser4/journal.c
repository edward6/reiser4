/*
    journal.c -- reiserfs filesystem journal common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

reiserfs_journal_t *reiserfs_journal_open(aal_device_t *device, 
    reiserfs_id_t pid) 
{
    reiserfs_plugin_t *plugin;
    reiserfs_journal_t *journal;
	
    aal_assert("umka-095", device != NULL, return NULL);
	
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    if (!(plugin = libreiser4_factory_find(REISERFS_JOURNAL_PLUGIN, pid)))
	libreiser4_factory_failed(goto error_free_journal, find, journal, pid);
	
    journal->plugin = plugin;
	
    if (!(journal->entity = libreiser4_plugin_call(goto error_free_journal, 
	plugin->journal_ops, open, device))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open journal %s on %s.", plugin->h.label, 
	    aal_device_name(device));
	goto error_free_journal;
    }
	
    return journal;

error_free_journal:
    aal_free(journal);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_journal_t *reiserfs_journal_create(aal_device_t *device, 
    void *params, reiserfs_id_t pid) 
{
    reiserfs_plugin_t *plugin;
    reiserfs_journal_t *journal;
	
    aal_assert("umka-095", device != NULL, return NULL);
	
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;
	
    if (!(plugin = libreiser4_factory_find(REISERFS_JOURNAL_PLUGIN, pid))) 
	libreiser4_factory_failed(goto error_free_journal, find, journal, pid);

    journal->plugin = plugin;
	
    if (!(journal->entity = libreiser4_plugin_call(goto error_free_journal, 
	plugin->journal_ops, create, device, params))) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create journal %s on %s.", plugin->h.label, 
	    aal_device_name(device));
	goto error_free_journal;
    }
	
    return journal;

error_free_journal:
    aal_free(journal);
    return NULL;
}

errno_t reiserfs_journal_replay(reiserfs_journal_t *journal) {
    aal_assert("umka-727", journal != NULL, return -1);
    
    if (libreiser4_plugin_call(return -1, journal->plugin->journal_ops, 
	replay, journal->entity)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't replay journal");
	return -1;
    }
    return 0;
}

errno_t reiserfs_journal_sync(reiserfs_journal_t *journal) {
    aal_assert("umka-100", journal != NULL, return -1);

    return libreiser4_plugin_call(return -1, journal->plugin->journal_ops, 
	sync, journal->entity);
}

#endif

void reiserfs_journal_close(reiserfs_journal_t *journal) {
    aal_assert("umka-102", journal != NULL, return);
    
    libreiser4_plugin_call(return, journal->plugin->journal_ops, 
	close, journal->entity);
    
    aal_free(journal);
}

