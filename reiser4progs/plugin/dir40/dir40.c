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
#  include <sys/types.h>
#  include <unistd.h>
#  include <time.h>
#endif

#include "dir40.h"

extern reiser4_plugin_t dir40_plugin;

static reiser4_core_t *core = NULL;

static roid_t dir40_objectid(dir40_t *dir) {
    aal_assert("umka-839", dir != NULL, return 0);
    
    return plugin_call(return 0, dir->key.plugin->key_ops, 
	get_objectid, dir->key.body);
}

static roid_t dir40_locality(dir40_t *dir) {
    aal_assert("umka-839", dir != NULL, return 0);
    
    return plugin_call(return 0, dir->key.plugin->key_ops, 
	get_locality, dir->key.body);
}

static errno_t dir40_rewind(reiser4_entity_t *entity) {
    reiser4_key_t key;
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-864", dir != NULL, return -1);
    
    /* Preparing key of the first entry in directory */
    key.plugin = dir->key.plugin;
    plugin_call(return -1, key.plugin->key_ops, build_direntry, 
	key.body, dir->hash_plugin, dir40_locality(dir), dir40_objectid(dir), 
	".");
	    
    if (core->tree_ops.lookup(dir->tree, &key, &dir->place) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find direntry of object 0x%llx.", 
	    dir40_objectid(dir));
	return -1;
    }

    /* Updating pointer to direntry item */
    if (core->tree_ops.item_body(dir->tree, &dir->place, &dir->direntry, NULL))
        return -1;
    
    dir->pos = 0;
    dir->place.pos.unit = 0;

    return 0;
}

/* This function grabs the stat data of directory */
static errno_t dir40_realize(dir40_t *dir) {
    aal_assert("umka-857", dir != NULL, return -1);	

    plugin_call(return -1, dir->key.plugin->key_ops, 
	build_generic, dir->key.body, KEY_STATDATA_TYPE, 
	dir40_locality(dir), dir40_objectid(dir), 0);
    
    /* Positioning to the dir stat data */
    if (core->tree_ops.lookup(dir->tree, &dir->key, &dir->place) != 1) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find stat data of directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	return -1;
    }
    
    return core->tree_ops.item_body(dir->tree, &dir->place, 
	&dir->statdata.data, &dir->statdata.len);
}

static errno_t dir40_read(reiser4_entity_t *entity, 
    reiser4_entry_hint_t *entry) 
{
    uint32_t count;
    reiser4_item_ops_t *item_ops;
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-844", dir != NULL, return -1);
    aal_assert("umka-845", entry != NULL, return -1);

    item_ops = &dir->direntry_plugin->item_ops;
    
    /* Getting count entries */
    if ((count = plugin_call(return -1, 
	    item_ops->common, count, dir->direntry)) == 0)
	return -1;

    if (dir->place.pos.unit >= count) {
	reiser4_key_t key;
	roid_t locality1, locality2;
	
	/* Switching to the rest of directory, which lies in other node */
	if (core->tree_ops.item_right(dir->tree, &dir->place))
	    return -1;
	
	/* Here we check is next item belongs to this directory */
	if (core->tree_ops.item_pid(dir->tree, &dir->place, ITEM_PLUGIN_TYPE) != 
		dir->direntry_plugin->h.id)
	    return -1;
	
	/* Getting key of the first item in the right neightbour */
	if (core->tree_ops.item_key(dir->tree, &dir->place, &key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item key by coord.");
	    return -1;
	}
	
	/* 
	    Getting locality of both keys in order to determine is they are 
	    mergeable.
	*/
	locality1 = plugin_call(return -1, dir->key.plugin->key_ops,
	    get_locality, dir->key.body);
	
	locality2 = plugin_call(return -1, dir->key.plugin->key_ops,
	    get_locality, key.body);
	
	/* Determining is items are mergeable */
	if (locality1 != locality2)
	    return -1;
	
	/* Items are mergeable, updating pointer to current directory item */
	if (core->tree_ops.item_body(dir->tree, &dir->place, 
		&dir->direntry, NULL))
	    return -1;
    }
    
    /* Getting next entry from the current direntry item */
    if ((plugin_call(return -1, item_ops->specific.direntry, 
	    entry, dir->direntry, dir->place.pos.unit, entry)))
	return -1;

    /* Updating positions */    
    dir->pos++; 
    dir->place.pos.unit++; 
    
    return 0;
}

/* Trying to guess hash in use by stat  dfata extention */
static reiser4_plugin_t *dir40_guess_hash(dir40_t *dir) {
    reiser4_plugin_t *plugin = NULL;

    return plugin;
}

static reiser4_entity_t *dir40_open(const void *tree, 
    reiser4_key_t *object) 
{
    dir40_t *dir;
    rpid_t statdata_pid;
    rpid_t direntry_pid;

    aal_assert("umka-836", tree != NULL, return NULL);
    aal_assert("umka-837", object != NULL, return NULL);
    aal_assert("umka-838", object->plugin != NULL, return NULL);
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    dir->plugin = &dir40_plugin;
    
    dir->key.plugin = object->plugin;
    plugin_call(goto error_free_dir, object->plugin->key_ops,
	assign, dir->key.body, object->body);
    
    /* Grabbing stat data */
    if (dir40_realize(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't grab stat data of  directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    /* 
	Initializing stat data plugin after dir40_realize function find and 
	grab pointer to the statdata item.
    */
    if ((statdata_pid = core->tree_ops.item_pid(dir->tree, &dir->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get stat data plugin id from the tree.");
	goto error_free_dir;
    }
    
    if (!(dir->statdata_plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, statdata_pid)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find stat data item plugin by its id 0x%x.", statdata_pid);
	goto error_free_dir;
    }
    
    
    
    /*
	FIXME-UMKA: Since hash plugin id will be stored as statdata extenstion, 
	we should initialize hash plugin of the directory after stat data plugin 
	init. But for awhile it is a hardcoded value. We will need to fix it after 
	stat data extentions will be supported.
    */
    if (!(dir->hash_plugin = core->factory_ops.plugin_ifind(HASH_PLUGIN_TYPE, 
	HASH_R5_ID)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find hash plugin by its id 0x%x.", HASH_R5_ID);
	goto error_free_dir;
    }
    
    /* Positioning to the first directory unit */
    if (dir40_rewind((reiser4_entity_t *)dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't rewind directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    /* 
	Initializing direntry plugin after dir40_rewind function find and grab 
	pointer	to the first direntry item.
    */
    if ((direntry_pid = core->tree_ops.item_pid(dir->tree, &dir->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't get direntry plugin id from the tree.");
	goto error_free_dir;
    }
    
    if (!(dir->direntry_plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, direntry_pid)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find direntry item plugin by its id 0x%x.", direntry_pid);
	goto error_free_dir;
    }
    
    return (reiser4_entity_t *)dir;

error_free_dir:
    aal_free(dir);
    return NULL;
}

#ifndef ENABLE_COMPACT

static reiser4_entity_t *dir40_create(const void *tree, 
    reiser4_key_t *parent, reiser4_key_t *object, 
    reiser4_object_hint_t *hint) 
{
    dir40_t *dir;
    
    reiser4_item_hint_t stat_hint;
    reiser4_statdata_hint_t stat;
    
    reiser4_item_hint_t direntry_hint;
    reiser4_direntry_hint_t direntry;
   
    reiser4_sdext_unix_hint_t unix_ext;
    
    roid_t objectid;
    roid_t locality;
    roid_t parent_locality;

    aal_assert("umka-743", parent != NULL, return NULL);
    aal_assert("umka-744", object != NULL, return NULL);
    aal_assert("umka-881", object->plugin != NULL, return NULL);
    aal_assert("umka-835", tree != NULL, return NULL);

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;
    
    dir->tree = tree;
    dir->plugin = &dir40_plugin;
    
    if (!(dir->hash_plugin = 
	core->factory_ops.plugin_ifind(HASH_PLUGIN_TYPE, hint->hash_pid)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find hash plugin by its id 0x%x.", hint->hash_pid);
	goto error_free_dir;
    }
    
    locality = plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, parent->body);
    
    objectid = plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, object->body);
    
    parent_locality = plugin_call(return NULL, 
	object->plugin->key_ops, get_locality, parent->body);
    
    if (!(dir->statdata_plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->statdata_pid)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find stat data item plugin by its id 0x%x.", hint->statdata_pid);
	goto error_free_dir;
    }
    
    if (!(dir->direntry_plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->direntry_pid)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find direntry item plugin by its id 0x%x.", hint->direntry_pid);
	goto error_free_dir;
    }

    /* 
	Initializing direntry item hint. This should be done earlier than 
	initializing of the stat data item hint, because we will need size 
	of direntry item durring stat data initialization.
    */
    aal_memset(&direntry_hint, 0, sizeof(direntry_hint));

    direntry.count = 2;
    direntry_hint.plugin = dir->direntry_plugin;
    
    direntry_hint.key.plugin = object->plugin; 
   
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_direntry, direntry_hint.key.body, dir->hash_plugin, 
	locality, objectid, ".");

    if (!(direntry.entry = 
	    aal_calloc(direntry.count*sizeof(*direntry.entry), 0)))
	goto error_free_dir;
    
    /* Preparing dot entry */
    direntry.entry[0].name = ".";
    
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_objid, &direntry.entry[0].objid, KEY_STATDATA_TYPE, 
	locality, objectid);
	
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entryid, &direntry.entry[0].entryid, dir->hash_plugin, 
	direntry.entry[0].name);
    
    /* Preparing dot-dot entry */
    direntry.entry[1].name = "..";
    
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_objid, &direntry.entry[1].objid, KEY_STATDATA_TYPE, 
	parent_locality, locality);
	
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entryid, &direntry.entry[1].entryid, dir->hash_plugin, 
	direntry.entry[1].name);
    
    direntry_hint.hint = &direntry;

    /* Initializing stat data hint */
    aal_memset(&stat_hint, 0, sizeof(stat_hint));
    
    stat_hint.plugin = dir->statdata_plugin;
    
    stat_hint.key.plugin = object->plugin;
    plugin_call(goto error_free_dir, object->plugin->key_ops,
	assign, stat_hint.key.body, object->body);
    
    /* Initializing stat data item hint. */
    stat.mode = S_IFDIR | 0755;
    stat.extmask = hint->sdext;
    stat.nlink = 2;
    stat.size = 2;
    
    unix_ext.uid = getuid();
    unix_ext.gid = getgid();
    unix_ext.atime = time(NULL);
    unix_ext.mtime = time(NULL);
    unix_ext.ctime = time(NULL);
    unix_ext.rdev = 0;

    unix_ext.bytes = plugin_call(goto error_free_dir, 
	dir->direntry_plugin->item_ops.common, estimate, ~0ul, 
	&direntry_hint);

    stat.extentions.count = 1;
    stat.extentions.hint[0] = &unix_ext;

    stat_hint.hint = &stat;
    
    /* Calling balancing code in order to insert statdata item into the tree */
    if (core->tree_ops.item_insert(tree, &stat_hint)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert stat data item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    /* Inserting the direntry item into the tree */
    if (core->tree_ops.item_insert(tree, &direntry_hint)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't insert direntry item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    aal_free(direntry.entry);
    
    dir->key.plugin = object->plugin;
    plugin_call(goto error_free_dir, object->plugin->key_ops,
	assign, dir->key.body, object->body);
    
    /* Grabbing the stat data item */
    if (dir40_realize(dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't grab stat data of  directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }

    /* Positioning onto first directory unit */
    if (dir40_rewind((reiser4_entity_t *)dir)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't rewind directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    return (reiser4_entity_t *)dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}

static errno_t dir40_add(reiser4_entity_t *entity, 
    reiser4_entry_hint_t *entry) 
{
    reiser4_item_hint_t hint;
    reiser4_direntry_hint_t direntry_hint;
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-844", dir != NULL, return -1);
    aal_assert("umka-845", entry != NULL, return -1);
   
    aal_memset(&hint, 0, sizeof(hint));
    aal_memset(&direntry_hint, 0, sizeof(direntry_hint));

    direntry_hint.count = 1;

    if (!(direntry_hint.entry = aal_calloc(sizeof(*entry), 0)))
	return -1;
    
    hint.hint = &direntry_hint;
   
    plugin_call(goto error_free_entry, dir->key.plugin->key_ops, 
	build_objid, &entry->objid, KEY_STATDATA_TYPE, 
	entry->objid.locality, entry->objid.objectid);
	
    plugin_call(goto error_free_entry, dir->key.plugin->key_ops, 
	build_entryid, &entry->entryid, dir->hash_plugin, entry->name);
    
    aal_memcpy(&direntry_hint.entry[0], entry, sizeof(*entry));
    
    hint.key.plugin = dir->key.plugin;
    
    plugin_call(goto error_free_entry, hint.key.plugin->key_ops, 
	build_direntry, hint.key.body, dir->hash_plugin, dir40_locality(dir), 
	dir40_objectid(dir), entry->name);
    
    hint.len = 0;
    hint.plugin = dir->direntry_plugin;
    
    /* Inserting the entry to the tree */
    if (core->tree_ops.item_insert(dir->tree, &hint)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't add entry \"%s\" to the thee.", entry->name);
	goto error_free_entry;
    }
    
    aal_free(direntry_hint.entry);
    
    return 0;
error_free_entry:
    aal_free(direntry_hint.entry);
    return -1;
}

#endif

static void dir40_close(reiser4_entity_t *entity) {
    aal_assert("umka-750", entity != NULL, return);
    aal_free(entity);
}

static uint32_t dir40_tell(reiser4_entity_t *entity) {
    aal_assert("umka-874", entity != NULL, return 0);
    return ((dir40_t *)entity)->pos;
}

static reiser4_plugin_t dir40_plugin = {
    .dir_ops = {
	.h = {
	    .handle = NULL,
	    .id = DIR_DIR40_ID,
	    .type = DIR_PLUGIN_TYPE,
	    .label = "dir40",
	    .desc = "Compound directory for reiserfs 4.0, ver. " VERSION,
	},
#ifndef ENABLE_COMPACT
	.create	    = dir40_create,
	.add	    = dir40_add,
	.remove	    = NULL,
#else
	.create	    = NULL,
	.add	    = NULL,
	.remove	    = NULL,
#endif
	.valid	    = NULL,
	.lookup	    = NULL,

	.open	    = dir40_open,
	.close	    = dir40_close,
	.rewind	    = dir40_rewind,
	.tell	    = dir40_tell,
	.read	    = dir40_read
    }
};

static reiser4_plugin_t *dir40_start(reiser4_core_t *c) {
    core = c;
    return &dir40_plugin;
}

plugin_register(dir40_start);

