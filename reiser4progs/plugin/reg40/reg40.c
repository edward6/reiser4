/*
    reg40.c -- reiser4 default regular file plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <time.h>
#endif

#include "reg40.h"

extern reiser4_plugin_t reg40_plugin;

static reiser4_core_t *core = NULL;

static roid_t reg40_objectid(reg40_t *reg) {
    aal_assert("umka-839", reg != NULL, return 0);
    
    return plugin_call(return 0, reg->key.plugin->key_ops, 
	get_objectid, reg->key.body);
}

static roid_t reg40_locality(reg40_t *reg) {
    aal_assert("umka-839", reg != NULL, return 0);
    
    return plugin_call(return 0, reg->key.plugin->key_ops, 
	get_locality, reg->key.body);
}

static errno_t reg40_reset(reiser4_entity_t *entity) {
    rpid_t pid;
    reiser4_key_t key;
    
    reg40_t *reg = (reg40_t *)entity;
    
    aal_assert("umka-864", reg != NULL, return -1);
    
    key.plugin = reg->key.plugin;
    plugin_call(return -1, key.plugin->key_ops, build_generic, 
	key.body, KEY_FILEBODY_TYPE, reg40_locality(reg), 
	reg40_objectid(reg), 0);
    
    if (core->tree_ops.lookup(reg->tree, &key, &reg->place) != 1) {
	aal_exception_error("Can't find stream of regular file 0x%llx.", 
	    reg40_objectid(reg));

	return -1;
    }

    if ((pid = core->tree_ops.item_pid(reg->tree, &reg->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_error("Can't get regular file plugin id from the tree.");
	return -1;
    }
    
    /* 
	FIXME-UMKA: Here should be the initializing of reg file body plugin and
       	pointer to body itself.
    */
    
/*    if (!(reg->direntry.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, pid)))
    {
	aal_exception_error("Can't find item plugin by its id 0x%x.", pid);
	return -1;
    }
    
    if (core->tree_ops.item_body(dir->tree, &dir->place, 
	    &dir->direntry.body, NULL))
        return -1;*/
    
    reg->offset = 0;
    reg->place.pos.unit = 0;

    return 0;
}

/* This function grabs the stat data of the reg file */
static errno_t reg40_realize(reg40_t *reg) {
    rpid_t pid;
    
    aal_assert("umka-857", reg != NULL, return -1);	

    plugin_call(return -1, reg->key.plugin->key_ops, build_generic, 
	reg->key.body, KEY_STATDATA_TYPE, reg40_locality(reg), 
	reg40_objectid(reg), 0);
    
    /* Positioning to the file stat data */
    if (core->tree_ops.lookup(reg->tree, &reg->key, &reg->place) != 1) {

	aal_exception_error("Can't find stat data of the file with oid 0x%llx.", 
	    reg40_objectid(reg));
	
	return -1;
    }
    
    /* 
	Initializing stat data plugin after reg40_realize function find and 
	grab pointer to the statdata item.
    */
    if ((pid = core->tree_ops.item_pid(reg->tree, &reg->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_error("Can't get stat data plugin id of the file 0x%llx.",
	    reg40_objectid(reg));
	
	return -1;
    }
    
    if (!(reg->statdata.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, pid)))
    {
	aal_exception_error("Can't find stat data item plugin "
	    "by its id 0x%x.", pid);
	return -1;
    }
    
    {
	errno_t res = core->tree_ops.item_body(reg->tree, &reg->place, 
	    &reg->statdata.body, NULL);

	if (res) return res;
    }
    
    return 0;
}

/* Reads n entries to passed buffer buff */
static uint64_t reg40_read(reiser4_entity_t *entity, 
    char *buff, uint64_t n)
{
    return 0;
}

static reiser4_entity_t *reg40_open(const void *tree, 
    reiser4_key_t *object) 
{
    reg40_t *reg;

    aal_assert("umka-836", tree != NULL, return NULL);
    aal_assert("umka-837", object != NULL, return NULL);
    aal_assert("umka-838", object->plugin != NULL, return NULL);
    
    if (!(reg = aal_calloc(sizeof(*reg), 0)))
	return NULL;
    
    reg->tree = tree;
    reg->plugin = &reg40_plugin;
    
    reg->key.plugin = object->plugin;
    plugin_call(goto error_free_reg, object->plugin->key_ops,
	assign, reg->key.body, object->body);
    
    /* Grabbing the stat data item */
    if (reg40_realize(reg)) {
	aal_exception_error("Can't grab stat data of the file "
	    "with oid 0x%llx.", reg40_objectid(reg));
	goto error_free_reg;
    }
    
    /* Positioning to the first directory unit */
    if (reg40_reset((reiser4_entity_t *)reg)) {

	aal_exception_error("Can't rewind directory with oid 0x%llx.", 
	    reg40_objectid(reg));
	
	goto error_free_reg;
    }
    
    return (reiser4_entity_t *)reg;

error_free_reg:
    aal_free(reg);
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiser4_entity_t *reg40_create(const void *tree, 
    reiser4_key_t *parent, reiser4_key_t *object, 
    reiser4_file_hint_t *hint) 
{
    reg40_t *reg;
    
    reiser4_item_hint_t stat_hint;
    reiser4_statdata_hint_t stat;
    
    reiser4_sdext_unix_hint_t unix_ext;
    
    roid_t objectid;
    roid_t locality;
    roid_t parent_locality;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);
    aal_assert("umka-881", object->plugin != NULL, return NULL);
    aal_assert("umka-835", tree != NULL, return NULL);

    if (!(reg = aal_calloc(sizeof(*reg), 0)))
	return NULL;
    
    reg->tree = tree;
    reg->plugin = &reg40_plugin;
    
    locality = plugin_call(return NULL, object->plugin->key_ops, 
	get_objectid, parent->body);
    
    objectid = plugin_call(return NULL, object->plugin->key_ops, 
	get_objectid, object->body);
    
    parent_locality = plugin_call(return NULL, object->plugin->key_ops, 
	get_locality, parent->body);
    
    if (!(reg->statdata.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->statdata_pid)))
    {
	aal_exception_error("Can't find stat data item plugin by its id 0x%x.", 
	    hint->statdata_pid);
	
	goto error_free_reg;
    }
    
    /* FIXME-UMKA: Here should be the initializing of some kind of body plugin */
/*    if (!(reg->direntry.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->direntry_pid)))
    {
	aal_exception_error("Can't find direntry item plugin by its id 0x%x.", 
	    hint->direntry_pid);
	
	goto error_free_dir;
    }*/

    /* Initializing the stat data hint */
    aal_memset(&stat_hint, 0, sizeof(stat_hint));
    
    stat_hint.plugin = reg->statdata.plugin;
    
    stat_hint.key.plugin = object->plugin;
    plugin_call(goto error_free_reg, object->plugin->key_ops,
	assign, stat_hint.key.body, object->body);
    
    /* Initializing stat data item hint. */
    stat.mode = S_IFDIR | 0755;
    stat.extmask = 1 << SDEXT_UNIX_ID;
    stat.nlink = 2;
    stat.size = 2;
    
    unix_ext.uid = getuid();
    unix_ext.gid = getgid();
    unix_ext.atime = time(NULL);
    unix_ext.mtime = time(NULL);
    unix_ext.ctime = time(NULL);
    unix_ext.rdev = 0;

    /* FIXME-UMKA: Here should be real file size in bytes */
    unix_ext.bytes = 0;

    stat.extentions.count = 1;
    stat.extentions.hint[0] = &unix_ext;

    stat_hint.hint = &stat;
    
    /* Calling balancing code in order to insert statdata item into the tree */
    if (core->tree_ops.item_insert(tree, &stat_hint)) {
	aal_exception_error("Can't insert stat data item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_reg;
    }
    
    /* FIXME-UMKA: Here should be inserting file body in the tree */
/*    if (core->tree_ops.item_insert(tree, &direntry_hint)) {
	aal_exception_error("Can't insert direntry item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }*/
    
    reg->key.plugin = object->plugin;
    plugin_call(goto error_free_reg, object->plugin->key_ops,
	assign, reg->key.body, object->body);
    
    /* Grabbing the stat data item */
    if (reg40_realize(reg)) {

        aal_exception_error("Can't grab stat data of  directory with "
	    "oid 0x%llx.", reg40_objectid(reg));
	
	goto error_free_reg;
    }

    /* Positioning onto first directory unit */
    if (reg40_reset((reiser4_entity_t *)reg)) {
	    
	aal_exception_error("Can't rewind directory with oid 0x%llx.", 
	    reg40_objectid(reg));
	
	goto error_free_reg;
    }
    
    return (reiser4_entity_t *)reg;

error_free_reg:
    aal_free(reg);
    return NULL;
}

static errno_t reg40_truncate(reiser4_entity_t *entity, 
    uint64_t n) 
{
    /* Sorry, not implemented yet! */
    return -1;
}

/* Adds n entries from buff to passed entity */
static uint64_t reg40_write(reiser4_entity_t *entity, 
    char *buff, uint64_t n) 
{
    return 0;
}

#endif

static void reg40_close(reiser4_entity_t *entity) {
    aal_assert("umka-1158", entity != NULL, return);
    aal_free(entity);
}

static uint64_t reg40_offset(reiser4_entity_t *entity) {
    aal_assert("umka-1159", entity != NULL, return 0);
    return ((reg40_t *)entity)->offset;
}

static errno_t reg40_seek(reiser4_entity_t *entity, 
    uint64_t offset) 
{
    reg40_t *reg = (reg40_t *)entity;
    
    aal_assert("umka-1130", entity != NULL, return 0);

    /* FIXME-UMKA: Not implemented yet! */

    reg->offset = offset;
    return -1;
}

static reiser4_plugin_t reg40_plugin = {
    .file_ops = {
	.h = {
	    .handle = NULL,
	    .id = FILE_REGULAR40_ID,
	    .type = FILE_PLUGIN_TYPE,
	    .label = "reg40",
	    .desc = "Regular file for reiserfs 4.0, ver. " VERSION,
	},
#ifndef ENABLE_COMPACT
        .create	    = reg40_create,
        .write	    = reg40_write,
        .truncate   = reg40_truncate,
#else
        .create	    = NULL,
        .write	    = NULL,
        .truncate   = NULL,
#endif
        .valid	    = NULL,
        .lookup	    = NULL,
        .open	    = reg40_open,
        .close	    = reg40_close,
        .reset	    = reg40_reset,
        .offset	    = reg40_offset,
        .seek	    = reg40_seek,
	.read	    = reg40_read
    }
};

static reiser4_plugin_t *reg40_start(reiser4_core_t *c) {
    core = c;
    return &reg40_plugin;
}

plugin_register(reg40_start);

