/*
    journal.c -- reiser4 filesystem journal common code.
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
reiser4_journal_t *reiser4_journal_open(
    reiser4_format_t *format,	/* format journal is going to be opened on */
    aal_device_t *device	/* device journal weill be opened on */
) {
    rid_t pid;
    reiser4_plugin_t *plugin;
    reiser4_journal_t *journal;
	
    aal_assert("umka-095", format != NULL, return NULL);
	
    /* Allocating memory for jouranl instance */
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    if ((pid = reiser4_format_journal_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid journal plugin id has been found.");
	goto error_free_journal;
    }
    
    /* Getting plugin by its id from plugin factory */
    if (!(plugin = libreiser4_factory_ifind(JOURNAL_PLUGIN_TYPE, pid))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find journal plugin by its id 0x%x.", pid);
	goto error_free_journal;
    }
    
    journal->device = device;

    /* 
	Initializing journal entity by means of calling "open" method from found 
	journal plugin.
    */
    if (!(journal->entity = plugin_call(goto error_free_journal, 
	plugin->journal_ops, open, format->entity))) 
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
reiser4_journal_t *reiser4_journal_create(
    reiser4_format_t *format,	/* format journal will be opened on */
    aal_device_t *device,	/* device journal will be created on */
    void *params		/* journal params (opaque pointer) */
) {
    rid_t pid;
    reiser4_plugin_t *plugin;
    reiser4_journal_t *journal;
	
    aal_assert("umka-095", format != NULL, return NULL);
	
    /* Allocating memory and finding plugin */
    if (!(journal = aal_calloc(sizeof(*journal), 0)))
	return NULL;

    if ((pid = reiser4_format_journal_pid(format)) == INVALID_PLUGIN_ID) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Invalid journal plugin id has been found.");
	goto error_free_journal;
    }
    
    if (!(plugin = libreiser4_factory_ifind(JOURNAL_PLUGIN_TYPE, pid)))  {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find journal plugin by its id 0x%x.", pid);
	goto error_free_journal;
    }
    
    journal->device = device;
	
    /* Initializing journal entity */
    if (!(journal->entity = plugin_call(goto error_free_journal, 
	plugin->journal_ops, create, format->entity, params))) 
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
errno_t reiser4_journal_replay(
    reiser4_journal_t *journal	/* journal to be replayed */
) {
    aal_assert("umka-727", journal != NULL, return -1);
    
    /* Calling plugin for actual replaying */
    if (plugin_call(return -1, journal->entity->plugin->journal_ops, 
	replay, journal->entity)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't replay journal");
	return -1;
    }
    return 0;
}

/* Saves journal strucres on jouranl's device */
errno_t reiser4_journal_sync(
    reiser4_journal_t *journal	/* journal to be saved */
) {
    aal_assert("umka-100", journal != NULL, return -1);

    return plugin_call(return -1, journal->entity->plugin->journal_ops, 
	sync, journal->entity);
}

/* Checks jouranl structure for validness */
errno_t reiser4_journal_valid(
    reiser4_journal_t *journal  /* jouranl to eb checked */
) {
    aal_assert("umka-830", journal != NULL, return -1);

    return plugin_call(return -1, journal->entity->plugin->journal_ops, 
	valid, journal->entity);
}

#endif

/* Closes journal by means of freeing all assosiated memory */
void reiser4_journal_close(
    reiser4_journal_t *journal	/* Jouranl to be closed */
) {
    aal_assert("umka-102", journal != NULL, return);
    
    plugin_call(return, journal->entity->plugin->journal_ops, 
	close, journal->entity);
    
    aal_free(journal);
}

