/*
    journal.c -- reiserfs filesystem journal common code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>

error_t reiserfs_journal_open(reiserfs_fs_t *fs, aal_device_t *device, int replay) {
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-094", fs != NULL, return -1);
    aal_assert("umka-095", fs->super != NULL, return -1);
    aal_assert("umka-096", device != NULL, return -1);
	
    if (fs->journal) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Journal already initialized.");
	return -1;
    }
	
    if (!(fs->journal = aal_calloc(sizeof(*fs->journal), 0)))
	return -1;
	
    fs->journal->device = device;
	
    id = reiserfs_super_journal_plugin(fs);
    if (!(plugin = reiserfs_plugins_find(REISERFS_JOURNAL_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find journal plugin by its identifier %x.", id);
	goto error_free_journal;
    }
	
    fs->journal->plugin = plugin;
	
    reiserfs_plugin_check_routine(plugin->journal, open, goto error_free_journal);
    if (!(fs->journal->entity = plugin->journal.open(device))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialize the journal.");
	goto error_free_journal;
    }
	
    /* Optional replaying the journal */
    reiserfs_plugin_check_routine(plugin->journal, replay, goto error_free_entity);
    if (replay && plugin->journal.replay(fs->journal->entity)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't replay the journal.");
	goto error_free_entity;
    }

    return 0;

error_free_entity:
    reiserfs_plugin_check_routine(plugin->journal, close, goto error_free_journal);
    plugin->journal.close(fs->journal->entity, 0);
error_free_journal:
    aal_free(fs->journal);
    fs->journal = NULL;
error:
    return -1;
}

error_t reiserfs_journal_create(reiserfs_fs_t *fs, aal_device_t *device, 
    reiserfs_params_opaque_t *journal_params) 
{
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_id_t id;
	
    aal_assert("umka-097", device != NULL, return -1);
    aal_assert("umka-098", journal_params != NULL, return -1);

    if (!(fs->journal = aal_calloc(sizeof(*fs->journal), 0)))
	return -1;
	
    fs->journal->device = device;
	
    id = reiserfs_super_journal_plugin(fs);
    if (!(plugin = reiserfs_plugins_find(REISERFS_JOURNAL_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find journal plugin by its identifier %x.", id);
	goto error_free_journal;
    }
    fs->journal->plugin = plugin;
	
    reiserfs_plugin_check_routine(plugin->journal, create, goto error_free_journal);
    if (!(fs->journal->entity = plugin->journal.create(device, journal_params))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't create journal.");
	goto error_free_journal;
    }

    return 0;

error_free_journal:
    aal_free(fs->journal);
    fs->journal = NULL;
error:
    return -1;
}

error_t reiserfs_journal_sync(reiserfs_fs_t *fs) {
    aal_assert("umka-099", fs != NULL, return -1);
    aal_assert("umka-100", fs->journal != NULL, return -1);

    reiserfs_plugin_check_routine(fs->journal->plugin->journal, sync, return -1);
    return fs->journal->plugin->journal.sync(fs->journal->entity);
}

error_t reiserfs_journal_reopen(reiserfs_fs_t *fs, aal_device_t *device, int replay) {
    reiserfs_journal_close(fs, 1);
    return reiserfs_journal_open(fs, device, replay);
}

void reiserfs_journal_close(reiserfs_fs_t *fs, int sync) {
    aal_assert("umka-101", fs != NULL, return);
    aal_assert("umka-102", fs->journal != NULL, return);

    reiserfs_plugin_check_routine(fs->journal->plugin->journal, close, return);
    fs->journal->plugin->journal.close(fs->journal->entity, sync);
    
    aal_free(fs->journal);
    fs->journal = NULL;
}

