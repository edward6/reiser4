/*
    journal.c -- reiserfs filesystem journal common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

/* 
    This function opens journal on specified device and returns instance of 
    opened journal.
*/
reiserfs_journal_t *reiserfs_journal_open(
    reiserfs_format_t *format,	/* format journal is going to be opened on */
    aal_device_t *device	/* device journal weill be opened on */
) {
    reiserfs_id_t pid;
    reiserfs_plugin_t *plugin;
    reiserfs_journal_t *journal;
	
    aal_assert("umka-095", format != NULL, return NULL);
	
    /* Allocating memory for jouranl instance */
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    if ((pid = reiserfs_format_journal_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid journal plugin id has been found.");
	goto error_free_journal;
    }
    
    /* Getting plugin by its id from plugin factory */
    if (!(plugin = libreiser4_factory_find_by_id(JOURNAL_PLUGIN_TYPE, pid)))
	libreiser4_factory_failed(goto error_free_journal, find, journal, pid);
	
    journal->plugin = plugin;
    journal->device = device;

    /* 
	Initializing journal entity by means of calling "open" method from found 
	journal plugin.
    */
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

/* Creates journal on specified jopurnal. Returns initialized instance */
reiserfs_journal_t *reiserfs_journal_create(
    reiserfs_format_t *format,	/* format journal will be opened on */
    aal_device_t *device,	/* device journal will be created on */
    void *params		/* journal params (opaque pointer) */
) {
    reiserfs_id_t pid;
    reiserfs_plugin_t *plugin;
    reiserfs_journal_t *journal;
	
    aal_assert("umka-095", format != NULL, return NULL);
	
    /* Allocating memory and finding plugin */
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    if ((pid = reiserfs_format_journal_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid journal plugin id has been found.");
	goto error_free_journal;
    }
    
    if (!(plugin = libreiser4_factory_find_by_id(JOURNAL_PLUGIN_TYPE, pid))) 
	libreiser4_factory_failed(goto error_free_journal, find, journal, pid);

    journal->plugin = plugin;
    journal->device = device;
	
    /* Initializing journal entity */
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

/* Replays specified journal. Returns error code */
errno_t reiserfs_journal_replay(
    reiserfs_journal_t *journal	/* journal to be replayed */
) {
    aal_assert("umka-727", journal != NULL, return -1);
    
    /* Calling plugin for actual replaying */
    if (libreiser4_plugin_call(return -1, journal->plugin->journal_ops, 
	replay, journal->entity)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't replay journal");
	return -1;
    }
    return 0;
}

/* Saves journal strucres on jouranl's device */
errno_t reiserfs_journal_sync(
    reiserfs_journal_t *journal	/* journal to be saved */
) {
    aal_assert("umka-100", journal != NULL, return -1);

    return libreiser4_plugin_call(return -1, journal->plugin->journal_ops, 
	sync, journal->entity);
}

/* Checks jouranl structure for validness */
errno_t reiserfs_journal_valid(
    reiserfs_journal_t *journal, /* jouranl to eb checked */
    int flags			 /* some flags to be used */
) {
    aal_assert("umka-830", journal != NULL, return -1);

    return libreiser4_plugin_call(return -1, journal->plugin->journal_ops, 
	valid, journal->entity, flags);
}

#endif

/* Closes journal by means of freeing all assosiated memory */
void reiserfs_journal_close(
    reiserfs_journal_t *journal	/* Jouranl to be closed */
) {
    aal_assert("umka-102", journal != NULL, return);
    
    libreiser4_plugin_call(return, journal->plugin->journal_ops, 
	close, journal->entity);
    
    aal_free(journal);
}

