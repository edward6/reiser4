/*
	journal40.c -- reiser4 default journal plugin.
	Copyright (C) 1996-2002 Hans Reiser.
*/

#include <reiserfs/reiserfs.h>
#include <aal/aal.h>

#include "journal40.h"

static reiserfs_journal40_t *reiserfs_journal40_init(aal_device_t *device) {
	reiserfs_journal40_t *journal;

	if (!(journal = aal_calloc(sizeof(*journal), 0)))
		return NULL;
	
	journal->device = device;
	
	/* 
		Reading journal header and checking it for 
		validness must be here.
	*/
	
	return journal;
}

static void reiserfs_journal40_done(reiserfs_journal40_t *journal) {
	/* Synchronizing of the journal header must be here. */
	aal_free(journal);
}

static int reiserfs_journal40_replay(reiserfs_journal40_t *journal) {
	/* Journal replaying must be here. */
	return 1;
}

reiserfs_plugin_t plugin_info = {
	.journal = {
		.h = {
			.handle = NULL,
			.id = 0x1,
			.type = REISERFS_JOURNAL_PLUGIN,
			.label = "journal40",
			.desc = "Default journal for reiserfs 4.0, ver. 0.1, "
				"Copyright (C) 1996-2002 Hans Reiser",
		},
		.init = (reiserfs_journal_opaque_t *(*)(aal_device_t *))reiserfs_journal40_init,
		.done = (void (*)(reiserfs_journal_opaque_t *))reiserfs_journal40_done,
		.replay = (int (*)(reiserfs_journal_opaque_t *))reiserfs_journal40_replay
	}
};

reiserfs_plugin_t *reiserfs_plugin_info() {
	return &plugin_info;
}

