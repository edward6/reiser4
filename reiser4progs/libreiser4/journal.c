/*
    journal.c -- reiserfs filesystem journal common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

error_t reiserfs_journal_open(reiserfs_fs_t *fs, int replay) {
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-094", fs != NULL, return -1);
    aal_assert("umka-095", fs->format != NULL, return -1);
	
    if (fs->journal) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Journal already opened.");
	return -1;
    }
	
    if (!(fs->journal = aal_calloc(sizeof(*fs->journal), 0)))
	return -1;
	
    id = reiserfs_format_journal_plugin_id(fs);
    if (!(plugin = libreiser4_factory_find_by_coord(REISERFS_JOURNAL_PLUGIN, id)))
	libreiser4_factory_find_failed(REISERFS_JOURNAL_PLUGIN, id, goto error_free_journal);
	
    fs->journal->plugin = plugin;
	
    if (!(fs->journal->entity = reiserfs_format_journal(fs))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open journal.");
	goto error_free_journal;
    }
	
    /* Optional replaying the journal */
    if (replay && libreiser4_plugin_call(goto error_free_entity, 
	plugin->journal, replay, fs->journal->entity)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't replay journal.");
	goto error_free_entity;
    }

    return 0;

error_free_entity:
    libreiser4_plugin_call(goto error_free_journal, plugin->journal, 
	close, fs->journal);
error_free_journal:
    aal_free(fs->journal);
    fs->journal = NULL;
error:
    return -1;
}

#ifndef ENABLE_COMPACT

error_t reiserfs_journal_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-099", fs != NULL, return -1);
    aal_assert("umka-100", fs->journal != NULL, return -1);

    return libreiser4_plugin_call(return -1, fs->journal->plugin->journal, 
	sync, fs->journal->entity);
}

#endif

void reiserfs_journal_close(reiserfs_fs_t *fs) {
    aal_assert("umka-101", fs != NULL, return);
    aal_assert("umka-102", fs->journal != NULL, return);

    aal_free(fs->journal);
    fs->journal = NULL;
}

