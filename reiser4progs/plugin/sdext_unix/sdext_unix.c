/*
    sdext_unix.c -- stat data exception plugin, that implements unix stat data 
    fields.
    
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include <aal/aal.h>
#include <reiser4/plugin.h>

#include "sdext_unix.h"

static reiserfs_core_t *core = NULL;

static errno_t sdext_unix_create(reiserfs_sdext_unix_t *ext, 
    reiserfs_sdext_unix_hint_t *hint) 
{
    aal_assert("umka-884", ext != NULL, return -1);
    aal_assert("umka-885", hint != NULL, return -1);
	
    sdext_unix_set_uid(ext, hint->uid);
    sdext_unix_set_gid(ext, hint->gid);
    sdext_unix_set_atime(ext, hint->atime);
    sdext_unix_set_mtime(ext, hint->mtime);
    sdext_unix_set_ctime(ext, hint->ctime);
    sdext_unix_set_rdev(ext, hint->rdev);
    sdext_unix_set_bytes(ext, hint->bytes);

    return 0;
}

static errno_t sdext_unix_open(reiserfs_sdext_unix_t *ext, 
    reiserfs_sdext_unix_hint_t *hint) 
{
    aal_assert("umka-886", ext != NULL, return -1);
    aal_assert("umka-887", hint != NULL, return -1);
	
    hint->uid = sdext_unix_get_uid(ext);
    hint->gid = sdext_unix_get_gid(ext);
    hint->atime = sdext_unix_get_atime(ext);
    hint->mtime = sdext_unix_get_mtime(ext);
    hint->ctime = sdext_unix_get_ctime(ext);
    hint->rdev = sdext_unix_get_rdev(ext);
    hint->bytes = sdext_unix_get_bytes(ext);

    return 0;
}

static int sdext_unix_confirm(reiserfs_sdext_unix_t *ext) {
    return 0;
}

static uint32_t sdext_unix_length(void) {
    return sizeof(reiserfs_sdext_unix_t);
}

static reiserfs_plugin_t sdext_unix_plugin = {
    .sdext_ops = {
	.h = {
	    .handle = NULL,
	    .id = REISERFS_UNIX_SDEXT,
	    .type = REISERFS_SDEXT_PLUGIN,
	    .label = "sdext_unix",
	    .desc = "Unix stat data extention for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
	.create = (errno_t (*)(void *, void *))sdext_unix_create,
	.open = (errno_t (*)(void *, void *))sdext_unix_open,
	
	.confirm = (int (*)(void *))sdext_unix_open,
	.length = (uint32_t (*)(void))sdext_unix_length
    }
};

static reiserfs_plugin_t *sdext_unix_entry(reiserfs_core_t *c) {
    core = c;
    return &sdext_unix_plugin;
}

libreiser4_factory_register(sdext_unix_entry);

