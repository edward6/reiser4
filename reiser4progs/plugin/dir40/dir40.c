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

static errno_t dir40_reset(reiser4_entity_t *entity) {
    rpid_t pid;
    reiser4_key_t key;
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-864", dir != NULL, return -1);
    
    /* Preparing key of the first entry in directory */
    key.plugin = dir->key.plugin;
    plugin_call(return -1, key.plugin->key_ops, build_direntry, key.body, 
	dir->hash, dir40_locality(dir), dir40_objectid(dir), ".");
	    
    if (core->tree_ops.lookup(dir->tree, &key, &dir->place) != 1) {
	aal_exception_error("Can't find direntry of object 0x%llx.", 
	    dir40_objectid(dir));
	return -1;
    }

    if ((pid = core->tree_ops.item_pid(dir->tree, &dir->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_error("Can't get direntry plugin id from the tree.");
	return -1;
    }
    
    if (!(dir->direntry.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, pid)))
    {
	aal_exception_error("Can't find direntry item plugin by its id 0x%x.", pid);
	return -1;
    }
    
    /* Updating pointer to direntry item */
    if (core->tree_ops.item_body(dir->tree, 
	    &dir->place, &dir->direntry.body, NULL))
        return -1;
    
    dir->pos = 0;
    dir->place.pos.unit = 0;

    return 0;
}

/* Trying to guess hash in use by stat  dfata extention */
static reiser4_plugin_t *dir40_guess_hash(dir40_t *dir) {
    /* 
	FIXME-UMKA: This functions should inspect stat data extention first. And
	only in the case there are not convenient plugin extention (hash plugin),
	it should use some default hash plugin id.
    */
    return core->factory_ops.plugin_ifind(HASH_PLUGIN_TYPE, HASH_R5_ID);
}

/* This function grabs the stat data of directory */
static errno_t dir40_realize(dir40_t *dir) {
    rpid_t pid;
    
    aal_assert("umka-857", dir != NULL, return -1);	

    plugin_call(return -1, dir->key.plugin->key_ops, 
	build_generic, dir->key.body, KEY_STATDATA_TYPE, 
	dir40_locality(dir), dir40_objectid(dir), 0);
    
    /* Positioning to the dir stat data */
    if (core->tree_ops.lookup(dir->tree, &dir->key, &dir->place) != 1) {
	aal_exception_error("Can't find stat data of directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	return -1;
    }
    
    /* 
	Initializing stat data plugin after dir40_realize function find and 
	grab pointer to the statdata item.
    */
    if ((pid = core->tree_ops.item_pid(dir->tree, &dir->place, 
	ITEM_PLUGIN_TYPE)) == INVALID_PLUGIN_ID)
    {
	aal_exception_error("Can't get stat data plugin id of the object 0x%llx.",
	    dir40_objectid(dir));
	return -1;
    }
    
    if (!(dir->statdata.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, pid)))
    {
	aal_exception_error("Can't find stat data item plugin by its id 0x%x.", pid);
	return -1;
    }
    
    {
	errno_t res = core->tree_ops.item_body(dir->tree, &dir->place, 
	    &dir->statdata.body, NULL);

	if (res) return res;
    }
    
    if (!(dir->hash = dir40_guess_hash(dir))) {
	aal_exception_error("Can't guess hash plugin for directory %llx.", 
	    dir40_objectid(dir));
	return -1;
    }

    return 0;
}

static int dir40_continue(reiser4_entity_t *entity, 
    reiser4_place_t *next_place) 
{
    rpid_t next_pid;
    reiser4_key_t next_key;

    roid_t dir_locality;
    roid_t next_locality;
	
    dir40_t *dir = (dir40_t *)entity;

    /* Switching to the rest of directory, which lies in other node */
    if (core->tree_ops.item_right(dir->tree, next_place))
        return 0;
    
    next_pid = core->tree_ops.item_pid(dir->tree, 
	next_place, ITEM_PLUGIN_TYPE);
    
    /* Here we check is next item belongs to this directory */
    if (next_pid != dir->direntry.plugin->h.id)
	return 0;
	
    /* Getting key of the first item in the right neightbour */
    if (core->tree_ops.item_key(dir->tree, next_place, &next_key)) {
        aal_exception_error("Can't get item key by coord.");
	return 0;
    }
	
    /* 
        Getting locality of both keys in order to determine is they are 
        mergeable.
    */
    dir_locality = plugin_call(return -1, dir->key.plugin->key_ops,
        get_locality, dir->key.body);
	
    next_locality = plugin_call(return -1, dir->key.plugin->key_ops,
        get_locality, next_key.body);
	
    /* Determining is items are mergeable */
    return (dir_locality == next_locality);
}

/* Reads n entries to passed buffer buff */
static uint64_t dir40_read(reiser4_entity_t *entity, 
    char *buff, uint64_t n)
{
    uint32_t i, count;
    reiser4_plugin_t *plugin;
    dir40_t *dir = (dir40_t *)entity;

    reiser4_entry_hint_t *entry = (reiser4_entry_hint_t *)buff;
    
    aal_assert("umka-844", dir != NULL, return 0);
    aal_assert("umka-845", entry != NULL, return 0);

    plugin = dir->direntry.plugin;
    
    /* Getting count entries */
    if ((count = plugin_call(return -1, plugin->item_ops, 
	    count, dir->direntry.body)) == 0)
	return -1;

    for (i = 0; i < n; i++) {
	if (dir->place.pos.unit >= count) {
	    reiser4_place_t place = dir->place;
	
	    if (!dir40_continue(entity, &place)) {
		dir->place = place;
		break;
	    }
	
	   /* Items are mergeable, updating pointer to current directory item */
	    if (core->tree_ops.item_body(dir->tree, &dir->place, 
		   &dir->direntry.body, NULL))
		break;
	}
    
	/* Getting next entry from the current direntry item */
	if ((plugin_call(break, plugin->item_ops.specific.direntry, 
		entry, dir->direntry.body, dir->place.pos.unit, entry)))
	    break;

	entry++;
	dir->pos++; dir->place.pos.unit++; 
    }
    
    return i;
}

static int dir40_lookup(reiser4_entity_t *entity, 
    char *name, reiser4_key_t *key) 
{
    reiser4_key_t k;
    reiser4_plugin_t *plugin;
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-1117", entity != NULL, return -1);
    aal_assert("umka-1118", name != NULL, return -1);

    aal_assert("umka-1119", key != NULL, return -1);
    aal_assert("umka-1120", key->plugin != NULL, return -1);

    /* Forming the directory key */
    k.plugin = dir->key.plugin;
    plugin_call(return -1, k.plugin->key_ops, build_direntry, k.body, 
	dir->hash, dir40_locality(dir), dir40_objectid(dir), name);
    
    plugin = dir->direntry.plugin;

    while (1) {

	reiser4_place_t place;
	
	if (plugin_call(return -1, plugin->item_ops, lookup, 
	    dir->direntry.body, &k, &dir->place.pos.unit) == 1) 
	{
	    roid_t locality;
	    reiser4_entry_hint_t entry;
	    
	    if ((plugin_call(return -1, plugin->item_ops.specific.direntry, 
		    entry, dir->direntry.body, dir->place.pos.unit, &entry)))
		return -1;

	    locality = plugin_call(return -1, key->plugin->key_ops,
		get_locality, &entry.objid);
	    
	    plugin_call(return -1, key->plugin->key_ops, build_generic,
		key->body, KEY_STATDATA_TYPE, locality, entry.objid.objectid, 0);
	    
	    return 1;
	}
	
	place = dir->place;
    	
	if (!dir40_continue(entity, &place)) {
	    dir->place = place;
	    return 0;
	}
	
	if (core->tree_ops.item_body(dir->tree, &dir->place, 
		&dir->direntry.body, NULL))
	    return -1;
    }
    
    return 0;
}

static reiser4_entity_t *dir40_open(const void *tree, 
    reiser4_key_t *object) 
{
    dir40_t *dir;

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
	aal_exception_error("Can't grab stat data of  directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    /* Positioning to the first directory unit */
    if (dir40_reset((reiser4_entity_t *)dir)) {
	aal_exception_error("Can't rewind directory with oid 0x%llx.", 
	    dir40_objectid(dir));
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
    reiser4_file_hint_t *hint) 
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
    
    if (!(dir->hash = core->factory_ops.plugin_ifind(HASH_PLUGIN_TYPE, 
	hint->hash_pid)))
    {
	aal_exception_error("Can't find hash plugin by its id 0x%x.", 
	    hint->hash_pid);
	goto error_free_dir;
    }
    
    locality = plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, parent->body);
    
    objectid = plugin_call(return NULL, 
	object->plugin->key_ops, get_objectid, object->body);
    
    parent_locality = plugin_call(return NULL, 
	object->plugin->key_ops, get_locality, parent->body);
    
    if (!(dir->statdata.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->statdata_pid)))
    {
	aal_exception_error("Can't find stat data item plugin by its id 0x%x.", 
	    hint->statdata_pid);
	
	goto error_free_dir;
    }
    
    if (!(dir->direntry.plugin = 
	core->factory_ops.plugin_ifind(ITEM_PLUGIN_TYPE, hint->direntry_pid)))
    {
	aal_exception_error("Can't find direntry item plugin by its id 0x%x.", 
	    hint->direntry_pid);
	
	goto error_free_dir;
    }

    /* 
	Initializing direntry item hint. This should be done earlier than 
	initializing of the stat data item hint, because we will need size 
	of direntry item durring stat data initialization.
    */
    aal_memset(&direntry_hint, 0, sizeof(direntry_hint));

    direntry.count = 2;
    direntry_hint.plugin = dir->direntry.plugin;
    
    direntry_hint.key.plugin = object->plugin; 
   
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_direntry, direntry_hint.key.body, dir->hash, 
	locality, objectid, ".");

    {
	uint32_t size = direntry.count * sizeof(*direntry.entry);
	
	if (!(direntry.entry = aal_calloc(size, 0)))
	    goto error_free_dir;
    }
    
    /* Preparing dot entry */
    direntry.entry[0].name = ".";
    
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_objid, &direntry.entry[0].objid, KEY_STATDATA_TYPE, 
	locality, objectid);
	
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entryid, &direntry.entry[0].entryid, dir->hash, 
	direntry.entry[0].name);
    
    /* Preparing dot-dot entry */
    direntry.entry[1].name = "..";
    
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_objid, &direntry.entry[1].objid, KEY_STATDATA_TYPE, 
	parent_locality, locality);
	
    plugin_call(goto error_free_dir, object->plugin->key_ops, 
	build_entryid, &direntry.entry[1].entryid, dir->hash, 
	direntry.entry[1].name);
    
    direntry_hint.hint = &direntry;

    /* Initializing stat data hint */
    aal_memset(&stat_hint, 0, sizeof(stat_hint));
    
    stat_hint.plugin = dir->statdata.plugin;
    
    stat_hint.key.plugin = object->plugin;
    plugin_call(goto error_free_dir, object->plugin->key_ops,
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

    unix_ext.bytes = plugin_call(goto error_free_dir, 
	dir->direntry.plugin->item_ops, estimate, ~0ul, 
	&direntry_hint);

    stat.extentions.count = 1;
    stat.extentions.hint[0] = &unix_ext;

    stat_hint.hint = &stat;
    
    /* Calling balancing code in order to insert statdata item into the tree */
    if (core->tree_ops.item_insert(tree, &stat_hint)) {
	aal_exception_error("Can't insert stat data item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    /* Inserting the direntry item into the tree */
    if (core->tree_ops.item_insert(tree, &direntry_hint)) {
	aal_exception_error("Can't insert direntry item of object 0x%llx into "
	    "the thee.", objectid);
	goto error_free_dir;
    }
    
    aal_free(direntry.entry);
    
    dir->key.plugin = object->plugin;
    plugin_call(goto error_free_dir, object->plugin->key_ops,
	assign, dir->key.body, object->body);
    
    /* Grabbing the stat data item */
    if (dir40_realize(dir)) {
	aal_exception_error("Can't grab stat data of  directory with "
	    "oid 0x%llx.", dir40_objectid(dir));
	goto error_free_dir;
    }

    /* Positioning onto first directory unit */
    if (dir40_reset((reiser4_entity_t *)dir)) {
	aal_exception_error("Can't rewind directory with oid 0x%llx.", 
	    dir40_objectid(dir));
	goto error_free_dir;
    }
    
    return (reiser4_entity_t *)dir;

error_free_dir:
    aal_free(dir);
error:
    return NULL;
}

static errno_t dir40_truncate(reiser4_entity_t *entity, 
    uint64_t n) 
{
    /* Sorry, not implemented yet! */
    return -1;
}

/* Adds n entries from buff to passed entity */
static uint64_t dir40_write(reiser4_entity_t *entity, 
    char *buff, uint64_t n) 
{
    uint64_t i;
    reiser4_item_hint_t hint;
    dir40_t *dir = (dir40_t *)entity;
    reiser4_direntry_hint_t direntry_hint;
    
    reiser4_entry_hint_t *entry = (reiser4_entry_hint_t *)buff;
    
    aal_assert("umka-844", dir != NULL, return 0);
    aal_assert("umka-845", entry != NULL, return 0);
   
    aal_memset(&hint, 0, sizeof(hint));
    aal_memset(&direntry_hint, 0, sizeof(direntry_hint));

    direntry_hint.count = 1;

    /* 
	FIXME-UMKA: Here we have the funy situation. Direntry plugin can insert more
	than one entry in turn, but they should be sorted before. Else we will have 
	broken direntry. So, we should either perform a loop here in order to insert
	all n entris, or we should sort entries whenever (in direntry plugin or here).
    */
    if (!(direntry_hint.entry = aal_calloc(sizeof(*entry), 0)))
	return 0;
    
    hint.hint = &direntry_hint;
  
    for (i = 0; i < n; i++) {
	plugin_call(break, dir->key.plugin->key_ops, 
	    build_objid, &entry->objid, KEY_STATDATA_TYPE, 
	    entry->objid.locality, entry->objid.objectid);
	
	plugin_call(break, dir->key.plugin->key_ops, 
	    build_entryid, &entry->entryid, dir->hash, entry->name);
    
	aal_memcpy(&direntry_hint.entry[0], entry, sizeof(*entry));
    
	hint.key.plugin = dir->key.plugin;
    
	plugin_call(break, hint.key.plugin->key_ops, 
	    build_direntry, hint.key.body, dir->hash, dir40_locality(dir), 
	    dir40_objectid(dir), entry->name);
    
	hint.len = 0;
	hint.plugin = dir->direntry.plugin;
    
	/* Inserting the entry to the tree */
	if (core->tree_ops.item_insert(dir->tree, &hint)) {
	    aal_exception_error("Can't add entry \"%s\" to the thee.", 
		entry->name);
	    break;
	}
	entry++;
    }
    
    aal_free(direntry_hint.entry);
    return i;
}

#endif

static void dir40_close(reiser4_entity_t *entity) {
    aal_assert("umka-750", entity != NULL, return);
    aal_free(entity);
}

static uint64_t dir40_offset(reiser4_entity_t *entity) {
    aal_assert("umka-874", entity != NULL, return 0);
    return ((dir40_t *)entity)->pos;
}

static errno_t dir40_seek(reiser4_entity_t *entity, 
    uint64_t offset) 
{
    dir40_t *dir = (dir40_t *)entity;
    
    aal_assert("umka-1130", entity != NULL, return 0);

    /* FIXME-UMKA: Not implemented yet! */

    dir->pos = offset;
    return -1;
}

static reiser4_plugin_t dir40_plugin = {
    .file_ops = {
	.h = {
	    .handle = NULL,
	    .id = DIR_DIR40_ID,
	    .type = DIR_PLUGIN_TYPE,
	    .label = "dir40",
	    .desc = "Compound directory for reiserfs 4.0, ver. " VERSION,
	},
#ifndef ENABLE_COMPACT
        .create	    = dir40_create,
        .write	    = dir40_write,
        .truncate   = dir40_truncate,
#else
        .create	    = NULL,
        .write	    = NULL,
        .truncate   = NULL,
#endif
        .valid	    = NULL,
        .open	    = dir40_open,
        .close	    = dir40_close,
        .reset	    = dir40_reset,
        .offset	    = dir40_offset,
        .seek	    = dir40_seek,
        .lookup	    = dir40_lookup,
	.read	    = dir40_read
    }
};

static reiser4_plugin_t *dir40_start(reiser4_core_t *c) {
    core = c;
    return &dir40_plugin;
}

plugin_register(dir40_start);

