/*
    dir40.c -- reiser4 default directory object plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <sys/stat.h>
#endif

#include <reiser4/reiser4.h>
#include "dir40.h"

static reiserfs_core_t *core = NULL;

static oid_t dir40_objectid(reiserfs_dir40_t *dir) {
    aal_assert("umka-839", dir != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, dir->key.plugin->key_ops, 
	get_objectid, dir->key.body);
}

static oid_t dir40_locality(reiserfs_dir40_t *dir) {
    aal_assert("umka-839", dir != NULL, return 0);
    
    return libreiser4_plugin_call(return 0, dir->key.plugin->key_ops, 
	get_locality, dir->key.body);
}

static errno_t dir40_rewind(reiserfs_dir40_t *dir) {
    reiserfs_key_t key;
    
    aal_assert("umka-864", dir != NULL, return -1);
    
    /* Preparing key of the first entyr in directory */
    key.plugin = dir->key.plugin;

    libreiser4_plugin_call(return -1, key.plugin->key_ops, build_entry_full, 
	key.body, dir->hash_plugin, dir40_locality(dir), dir40_objectid(dir), ".");
	    
    if (core->tree_lookup(dir->tree, &key, &dir->place) != 1) {
	aal_throw_error(EO_OK, "Can't find direntry of object %llx.\n", 
	    dir40_objectid(dir));
	return -1;
    }

    /* Updating pointer to direntry item */
    if (core->tree_data(dir->tree, &dir->place, &dir->direntry, NULL))
        return -1;
    
    dir->pos = 0;
    dir->place.pos.unit = 0;

    return 0;
}

/* This function grabs the stat data of directory */
static errno_t dir40_realize(reiserfs_dir40_t *dir) {
    aal_assert("umka-857", dir != NULL, return -1);	

    /* FIXME-UMKA: Here should not be hardcoded key minor */
    libreiser4_plugin_call(return -1, dir->key.plugin->key_ops, build_generic_full, 
	dir->key.body, KEY40_STATDATA_MINOR, dir40_locality(dir), dir40_objectid(dir), 0);
    
    /* Positioning to the dir stat data */
    if (core->tree_lookup(dir->tree, &dir->key, &dir->place) != 1) {
	aal_throw_error(EO_OK, "Can't find stat data of directory with oid %llx.\n", 
	    dir40_objectid(dir));
	return -1;
    }
    
    return core->tree_data(dir->tree, &dir->place, 
	&dir->statdata.data, &dir->statdata.len);
}

static errno_t dir40_read(reiserfs_dir40_t *dir, 
    reiserfs_entry_hint_t *entry) 
{
    uint32_t count;
    reiserfs_item_ops_t *item_ops;
    
    aal_assert("umka-844", dir != NULL, return -1);
    aal_assert("umka-845", entry != NULL, return -1);

    item_ops = &dir->direntry_plugin->item_ops;
    
    /* Getting count entries */
    if ((count = libreiser4_plugin_call(return -1, 
	    item_ops->common, count, dir->direntry)) == 0)
	return -1;
    
    if (dir->place.pos.unit >= count) {
	reiserfs_entry_hint_t prev_entry;
	reiserfs_entry_hint_t next_entry;
    
	if ((libreiser4_plugin_call(return -1, item_ops->specific.direntry, 
		get_entry, dir->direntry, count - 1, &prev_entry)))
	    return -1;
	
	/* Switching to the rest of directory, which lies in other node */
	if (core->tree_right(dir->tree, &dir->place))
	    return -1;
	
	/* Here we check is next item belongs to this directory */
	if (core->tree_pid(dir->tree, &dir->place, REISERFS_ITEM_PLUGIN) != 
		dir->direntry_plugin->h.id)
	    return -1;
	
	if (core->tree_data(dir->tree, &dir->place, &dir->direntry, NULL))
	    return -1;
	
	if ((libreiser4_plugin_call(return -1, item_ops->specific.direntry, 
		get_entry, dir->direntry, dir->place.pos.unit, &next_entry)))
	    return -1;

	if (prev_entry.objid.locality != next_entry.objid.locality)
	    return -1;
    }
    
    /* Getting next entry from the current direntry item */
    if ((libreiser4_plugin_call(return -1, item_ops->specific.direntry, 
	    get_entry, dir->direntry, dir->place.pos.unit, entry)))
	return -1;

    /* Updating positions */    
    dir->pos++; 
    dir->place.pos.unit++; 
    
    return 0;
}

static reiserfs_dir40_t *dir40_open(const void *tree, 
    reiserfs_key_t *object) 
{
    uint32_t key_size;
    reiserfs_dir40_t *dir;
    reiserfs_id_t statdata_pid;
    reiserfs_id_t direntry_pid;

    aal_assert("umka-836", tree != NULL, return NULL);
    aal_assert("umka-837", object != NULL, return NULL);
    aal_assert("umka-838", object->plugin != NULL, return NULL);
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    
    key_size = libreiser4_plugin_call(goto error_free_dir, 
	object->plugin->key_ops, size,);
    
    dir->key.plugin = object->plugin;
    aal_memcpy(dir->key.body, object->body, key_size);
    
    /* Grabbing stat data */
    if (dir40_realize(dir)) {
	aal_throw_error(EO_OK, "Can't grab stat data of  directory with oid %llx.\n", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    /* 
	Initializing stat data plugin after dir40_realize function find and grab 
	pointer to the statdata item.
    */
    if ((statdata_pid = core->tree_pid(dir->tree, &dir->place, 
	REISERFS_ITEM_PLUGIN)) == REISERFS_INVAL_PLUGIN)
    {
	aal_throw_error(EO_OK, "Can't get stat data plugin id from the tree.\n");
	goto error_free_dir;
    }
    
    if (!(dir->statdata_plugin = core->factory_find(REISERFS_ITEM_PLUGIN, 
	statdata_pid)))
    {
	libreiser4_factory_failed(goto error_free_dir, find, 
	    statdata, statdata_pid);
    }
    
    /*
	Since hash plugin id will be stored as statdata extenstion, we should initialize
	hash plugin of the directory after stat data oplugin initialization. But for awhile
	it is a hardcoded value. We will need to fix it after stat data extentions will be 
	supported.
    */
    if (!(dir->hash_plugin = core->factory_find(REISERFS_HASH_PLUGIN, 
	REISERFS_R5_HASH_ID)))
    {
	libreiser4_factory_failed(goto error_free_dir, find, 
	    hash, REISERFS_R5_HASH_ID);
    }
    
    /* Positioning to the first directory unit */
    if (dir40_rewind(dir)) {
	aal_throw_error(EO_OK, "Can't rewind directory with oid %llx.\n", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    /* 
	Initializing direntry plugin after dir40_rewind function find and grab pointer
	to the first direntry item.
    */
    if ((direntry_pid = core->tree_pid(dir->tree, &dir->place, 
	REISERFS_ITEM_PLUGIN)) == REISERFS_INVAL_PLUGIN)
    {
	aal_throw_error(EO_OK, "Can't get direntry plugin id from the tree.\n");
	goto error_free_dir;
    }
    
    if (!(dir->direntry_plugin = core->factory_find(REISERFS_ITEM_PLUGIN, 
	direntry_pid)))
    {
	libreiser4_factory_failed(goto error_free_dir, find, 
	    direntry, direntry_pid);
    }
    
    return dir;

error_free_dir:
    aal_free(dir);
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiserfs_dir40_t *dir40_create(const void *tree, 
    reiserfs_key_t *parent, reiserfs_key_t *object, 
    reiserfs_object_hint_t *hint) 
{
    uint32_t key_size;
    reiserfs_dir40_t *dir;
    reiserfs_item_hint_t item;
    reiserfs_stat_hint_t stat;
    reiserfs_direntry_hint_t direntry;
   
    oid_t objectid;
    oid_t parent_objectid;
    oid_t parent_locality;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);
    aal_assert("umka-881", object->plugin != NULL, return NULL);
    aal_assert("umka-835", tree != NULL, return NULL);

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    
    if (!(dir->hash_plugin = core->factory_find(REISERFS_HASH_PLUGIN, hint->hash_pid)))
	libreiser4_factory_failed(goto error_free_dir, find, hash, hint->hash_pid);
    
    parent_objectid = libreiser4_plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, parent->body);
    
    parent_locality = libreiser4_plugin_call(return NULL, 
	object->plugin->key_ops, get_locality, parent->body);
    
    objectid = libreiser4_plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, object->body);
    
    aal_memset(&item, 0, sizeof(item));
    
    if (!(dir->statdata_plugin = core->factory_find(REISERFS_ITEM_PLUGIN, 
	hint->statdata_pid)))
    {
	libreiser4_factory_failed(goto error_free_dir, find, 
	    statdata, hint->statdata_pid);
    }
    
    if (!(dir->direntry_plugin = core->factory_find(REISERFS_ITEM_PLUGIN, 
	hint->direntry_pid)))
    {
	libreiser4_factory_failed(goto error_free_dir, find, 
	    direntry, hint->direntry_pid);
    }
    
    key_size = libreiser4_plugin_call(goto error_free_dir, 
	object->plugin->key_ops, size,);
    
    /* Initializing stat data hint */
    item.plugin = dir->statdata_plugin;
    item.type = item.plugin->h.id; 

    item.key.plugin = object->plugin;
    aal_memcpy(item.key.body, object->body, key_size);
    
    stat.mode = S_IFDIR | 0755;
    stat.extmask = 0;
    stat.nlink = 2;
    stat.size = 0;

    item.hint = &stat;

    /* Calling balancing code in order to insert statdata item into the tree */
    if (core->tree_insert(tree, &item)) {
	aal_throw_error(EO_OK, "Can't insert stat data item of object %llx into "
	    "the thee.\n", objectid);
	goto error_free_dir;
    }
    
    aal_memset(&item, 0, sizeof(item));

    /* Initializing direntry hint */
    item.plugin = dir->direntry_plugin;
    item.type = item.plugin->h.id; 
    
    item.key.plugin = object->plugin; 
    aal_memcpy(item.key.body, parent, key_size);
    
    direntry.count = 2;
   
    libreiser4_plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entry_full, item.key.body, dir->hash_plugin, parent_objectid, 
	objectid, ".");
    
    if (!(direntry.entry = aal_calloc(direntry.count*sizeof(*direntry.entry), 0)))
	goto error_free_dir;
    
    /* Preparing dot entry */
    direntry.entry[0].name = ".";
    
    libreiser4_plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_generic_short, &direntry.entry[0].objid, KEY40_STATDATA_MINOR, 
	parent_objectid, objectid);
	
    libreiser4_plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entry_short, &direntry.entry[0].entryid, dir->hash_plugin, 
	direntry.entry[0].name);
    
    /* Preparing dot-dot entry */
    direntry.entry[1].name = "..";
    
    libreiser4_plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_generic_short, &direntry.entry[1].objid, KEY40_STATDATA_MINOR, 
	parent_locality, parent_objectid);
	
    libreiser4_plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entry_short, &direntry.entry[1].entryid, dir->hash_plugin, 
	direntry.entry[1].name);
    
    item.hint = &direntry;
    
    /* Inserting the direntry item into the tree */
    if (core->tree_insert(tree, &item)) {
	aal_throw_error(EO_OK, "Can't insert direntry item of object %llx into "
	    "the thee.\n", objectid);
	goto error_free_dir;
    }
    
    aal_free(direntry.entry);
    
    dir->key.plugin = object->plugin;
    aal_memcpy(dir->key.body, object->body, key_size);
    
    /* Grabbing the stat data item */
    if (dir40_realize(dir)) {
	aal_throw_error(EO_OK, "Can't grab stat data of  directory with oid %llx.\n", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }

    /* Positioning onto first directory unit */
    if (dir40_rewind(dir)) {
	aal_throw_error(EO_OK, "Can't rewind directory with oid %llx.\n", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    return dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}

static errno_t dir40_add(reiserfs_dir40_t *dir, 
    reiserfs_entry_hint_t *entry) 
{
    aal_assert("umka-844", dir != NULL, return -1);
    aal_assert("umka-845", entry != NULL, return -1);

    return -1;
}

#endif

static void dir40_close(reiserfs_dir40_t *dir) {
    aal_assert("umka-750", dir != NULL, return);
    aal_free(dir);
}

static uint32_t dir40_tell(reiserfs_dir40_t *dir) {
    aal_assert("umka-874", dir != NULL, return 0);
    return dir->pos;
}

static reiserfs_plugin_t dir40_plugin = {
    .dir_ops = {
	.h = {
	    .handle = NULL,
	    .id = REISERFS_DIR40_ID,
	    .type = REISERFS_OBJECT_PLUGIN,
	    .label = "dir40",
	    .desc = "Directory for reiserfs 4.0, ver. 0.1, "
		"Copyright (C) 1996-2002 Hans Reiser",
	},
#ifndef ENABLE_COMPACT
	.create = (reiserfs_entity_t *(*)(const void *, reiserfs_key_t *, 
	    reiserfs_key_t *, reiserfs_object_hint_t *))dir40_create,
	
	.add = (errno_t (*)(reiserfs_entity_t *, reiserfs_entry_hint_t *))
	    dir40_read,
	
	.check = NULL,
#else
	.create = NULL,
	.check = NULL,
	.add = NULL,
#endif
	.open = (reiserfs_entity_t *(*)(const void *, reiserfs_key_t *))
	    dir40_open,

	.confirm = NULL,
	.close = (void (*)(reiserfs_entity_t *))dir40_close,
	.rewind = (errno_t (*)(reiserfs_entity_t *))dir40_rewind,
	.tell = (uint32_t (*)(reiserfs_entity_t *))dir40_tell,

	.read = (errno_t (*)(reiserfs_entity_t *, reiserfs_entry_hint_t *))
	    dir40_read,

	.lookup = NULL
    }
};

static reiserfs_plugin_t *dir40_entry(reiserfs_core_t *c) {
    core = c;
    return &dir40_plugin;
}

libreiser4_factory_register(dir40_entry);

