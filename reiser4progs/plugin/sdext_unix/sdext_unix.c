/*
    sdext_unix.c -- stat data exception plugin, that implements unix stat data 
    fields.
    
    Copyright 1996-2002 (C) Hans Reiser.
    Author Yury Umanets.
*/

#include "sdext_unix.h"

static reiser4_core_t *core = NULL;

static errno_t sdext_unix_init(reiser4_body_t *body, 
    reiser4_sdext_unix_hint_t *hint) 
{
    sdext_unix_t *ext;
    
    aal_assert("umka-884", body != NULL, return -1);
    aal_assert("umka-885", hint != NULL, return -1);
	
    ext = (sdext_unix_t *)body;
    
    sdext_unix_set_uid(ext, hint->uid);
    sdext_unix_set_gid(ext, hint->gid);
    sdext_unix_set_atime(ext, hint->atime);
    sdext_unix_set_mtime(ext, hint->mtime);
    sdext_unix_set_ctime(ext, hint->ctime);
    sdext_unix_set_rdev(ext, hint->rdev);
    sdext_unix_set_bytes(ext, hint->bytes);

    return 0;
}

static errno_t sdext_unix_open(reiser4_body_t *body, 
    reiser4_sdext_unix_hint_t *hint) 
{
    sdext_unix_t *ext;
    
    aal_assert("umka-886", body != NULL, return -1);
    aal_assert("umka-887", hint != NULL, return -1);

    ext = (sdext_unix_t *)body;
    
    hint->uid = sdext_unix_get_uid(ext);
    hint->gid = sdext_unix_get_gid(ext);
    hint->atime = sdext_unix_get_atime(ext);
    hint->mtime = sdext_unix_get_mtime(ext);
    hint->ctime = sdext_unix_get_ctime(ext);
    hint->rdev = sdext_unix_get_rdev(ext);
    hint->bytes = sdext_unix_get_bytes(ext);

    return 0;
}

static int sdext_unix_confirm(reiser4_body_t *body) {
    aal_assert("umka-1009", body != NULL, return -1);
    return 0;
}

static uint32_t sdext_unix_length(void) {
    return sizeof(sdext_unix_t);
}

static reiser4_plugin_t sdext_unix_plugin = {
    .sdext_ops = {
	.h = {
	    .handle = NULL,
	    .id = SDEXT_UNIX_ID,
	    .type = SDEXT_PLUGIN_TYPE,
	    .label = "sdext_unix",
	    .desc = "Unix stat data extention for reiserfs 4.0, ver. " VERSION,
	},
	.init	 = sdext_unix_init,
	.open	 = sdext_unix_open,
	.confirm = sdext_unix_confirm,
	.length	 = sdext_unix_length
    }
};

static reiser4_plugin_t *sdext_unix_start(reiser4_core_t *c) {
    core = c;
    return &sdext_unix_plugin;
}

plugin_register(sdext_unix_start);

