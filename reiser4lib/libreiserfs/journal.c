/*
	journal.c -- reiserfs filesystem journal common code.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

int reiserfs_journal_open(reiserfs_fs_t *fs, aal_device_t *device, int replay) {
    reiserfs_plugin_id_t id;
    reiserfs_plugin_t *plugin;
	
    ASSERT(fs != NULL, return 0);
    ASSERT(fs->super != NULL, return 0);
    ASSERT(device != NULL, return 0);
	
    if (fs->journal) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-013", 
	    "Journal already initialized.");
	return 0;
    }
	
    if (!(fs->journal = aal_calloc(sizeof(*fs->journal), 0)))
	return 0;
	
    fs->journal->device = device;
	
    id = reiserfs_super_journal_plugin(fs);
    if (!(plugin = reiserfs_plugin_find(REISERFS_JOURNAL_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-014", 
	    "Can't find journal plugin by its identifier %x.", id);
	goto error_free_journal;
    }
	
    fs->journal->plugin = plugin;
	
    reiserfs_plugin_check_routine(plugin->journal, open, goto error_free_journal);
    if (!(fs->journal->entity = plugin->journal.open(device))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-015", 
	    "Can't initialize the journal plugin.");
	goto error_free_journal;
    }
	
    /* Optional replaying the journal */
    reiserfs_plugin_check_routine(plugin->journal, replay, goto error_free_entity);
    if (replay && !plugin->journal.replay(fs->journal->entity)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-016", 
	    "Can't replay the journal.");
	goto error_free_entity;
    }

    return 1;

error_free_entity:
    reiserfs_plugin_check_routine(plugin->journal, close, goto error_free_journal);
    plugin->journal.close(fs->journal->entity, 0);
	
    fs->journal->entity = NULL;
error_free_journal:
    aal_free(fs->journal);
    fs->journal = NULL;
error:
    return 0;
}

int reiserfs_journal_create(reiserfs_fs_t *fs, aal_device_t *device, 
    reiserfs_params_opaque_t *journal_params) 
{
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_id_t id;
	
    ASSERT(device != NULL, return 0);
    ASSERT(journal_params != NULL, return 0);

    if (!(fs->journal = aal_calloc(sizeof(*fs->journal), 0)))
	return 0;
	
    fs->journal->device = device;
	
    id = reiserfs_super_journal_plugin(fs);
    if (!(plugin = reiserfs_plugin_find(REISERFS_JOURNAL_PLUGIN, id))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-028", 
	    "Can't find journal plugin by its identifier %x.", id);
	goto error_free_journal;
    }
    fs->journal->plugin = plugin;
	
    reiserfs_plugin_check_routine(plugin->journal, create, goto error_free_journal);
	
    if (!(fs->journal->entity = plugin->journal.create(device, journal_params))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-029", 
	    "Can't create journal.");
	goto error_free_journal;
    }
	
    return 1;
	
error_free_journal:
    aal_free(fs->journal);
    fs->journal = NULL;
error:
    return 0;
}

int reiserfs_journal_reopen(reiserfs_fs_t *fs, aal_device_t *device, int replay) {
    reiserfs_journal_close(fs, 1);
    return reiserfs_journal_open(fs, device, replay);
}

void reiserfs_journal_close(reiserfs_fs_t *fs, int sync) {
    ASSERT(fs != NULL, return);
    ASSERT(fs->journal != NULL, return);
	
    reiserfs_plugin_check_routine(fs->journal->plugin->journal, close, return);
    fs->journal->plugin->journal.close(fs->journal->entity, sync);
    fs->journal->entity = NULL;
	
    aal_free(fs->journal);
    fs->journal = NULL;
}

