/*
    object.c -- common code for files and directories.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>
#include <sys/stat.h>

/* 
    Tries to guess object plugin type passed first item plugin and item body. Most
    possible that passed item body is stat data body.
*/
static reiserfs_plugin_t *reiserfs_object_guess_plugin(reiserfs_object_t *object) {
    void *item_body;
    reiserfs_plugin_t *item_plugin;
    
    /* Getting plugin for the first object item (most probably stat data item) */
    if (!(item_plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find first item plugin.");
	return NULL;
    }
    
    /* Getting first item body */
    if (!(item_body = reiserfs_node_item_body(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find first item plugin.");
	return NULL;
    }
    
    /* FIXME-UMKA: Here should be real detecting instead of hardcoded plugin */
    return libreiser4_factory_find(REISERFS_DIR_PLUGIN, 0x0);
}

/* 
    Performs lookup of object statdata by object name. result of lookuping are 
    stored in passed object fileds. Returns error code or 0 if there is no errors.
    This function also supports symlinks and it rather might be called "stat", by
    means of work it performs.
*/
static errno_t reiserfs_object_lookup(
    reiserfs_object_t *object,	    /* object lookup will be performed for */
    const char *name,		    /* name to be parsed */
    reiserfs_key_t *parent	    /* key of parent stat data */
) {
    void *object_entity;
    reiserfs_plugin_t *object_plugin;
    
    char track[4096], path[4096];
    char *pointer = NULL, *dirname = NULL;

    aal_assert("umka-682", object != NULL, return -1);
    aal_assert("umka-681", name != NULL, return -1);
    aal_assert("umka-685", parent != NULL, return -1);
    
    aal_memset(path, 0, sizeof(path));
    aal_memset(track, 0, sizeof(track));
    
    aal_strncpy(path, name, sizeof(path));
    
    if (path[0] != '.' || path[0] == '/')
	track[aal_strlen(track)] = '/';
   
    if (path[0] == '/')
	pointer = &path[1]; 
    else
	pointer = &path[0];

    /* Main big loop all work is performed inside wich */
    while (1) {
	uint16_t mode;
	void *item_body;
	reiserfs_plugin_t *item_plugin;

	/* FIXME-UMKA: Hardcoded key40 key type */
	reiserfs_key_set_type(&object->key, KEY40_STATDATA_MINOR);
	reiserfs_key_set_offset(&object->key, 0);
	
	if (reiserfs_tree_lookup(object->fs->tree, REISERFS_LEAF_LEVEL, 
	    &object->key, &object->coord) != 1) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory \"%s\".", track);
	    return -1;
	}
	
	if (!(item_body = reiserfs_node_item_body(object->coord.cache->node, 
	    object->coord.pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item body. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.cache->node->block), 
		object->coord.pos.item);
	    return -1;
	}
	
	if (!(item_plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	    object->coord.pos.item)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item plugin. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.cache->node->block),
		object->coord.pos.item);
	    return -1;
	}

	/* 
	    Checking for mode. It is used in order to know is current entry link or 
	    not and is this mode valid one at all.
	*/
	mode = libreiser4_plugin_call(return -1, 
	    item_plugin->item_ops.specific.statdata, get_mode, item_body);

	if (!S_ISLNK(mode) && !S_ISDIR(mode) && !S_ISREG(mode)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"%s has invalid object type.", track);
	    return -1;
	}
		
	if (S_ISLNK(mode)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Sorry, opening objects by link is not supported yet!");
	    return -1;
	}

	/* It will be useful when symlinks ready */
	reiserfs_key_set_locality(parent, reiserfs_key_get_locality(&object->key));
	reiserfs_key_set_objectid(parent, reiserfs_key_get_objectid(&object->key));

	if (!(dirname = aal_strsep(&pointer, "/")))
	    break;
		
	if (!aal_strlen(dirname))
	    continue;
	
	aal_strncat(track, dirname, aal_strlen(dirname));
	
	/* 
	    Here we should get dir plugin id from the statdata and using it try find 
	    needed entry inside it.
	*/
	if (!(object_plugin = reiserfs_object_guess_plugin(object))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't guess object plugin for parent of %s.", track);
	    return -1;
	}

	if (!object_plugin->dir_ops.lookup) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Method \"lookup\" is not implemented in %s plugin.", 
		object_plugin->h.label);
	    return -1;
	}
	
	if (object_plugin->h.type == REISERFS_DIR_PLUGIN) {
	    reiserfs_entry_hint_t entry;
	    
	    if (!(object_entity = libreiser4_plugin_call(return -1, 
		object_plugin->dir_ops, open, object->fs->tree, &object->key)))
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't open parent of directory \"%s\".", track);
		return -1;
	    }
	    
	    entry.name = dirname;
	    if (object_plugin->dir_ops.lookup(object_entity, &entry)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find entry \"%s\".", entry.name);
		
		libreiser4_plugin_call(return -1, object_plugin->dir_ops, 
		    close, object_entity);
		return -1;
	    }
	    
	    libreiser4_plugin_call(return -1, object_plugin->dir_ops, 
		close, object_entity);

	    /* Updating object key by found objectid and locality */
	    reiserfs_key_set_objectid(&object->key, entry.objid.objectid);
	    reiserfs_key_set_locality(&object->key, entry.objid.locality);
	} else {

	    /* 
		Here we should check is found object type may contain entries (probably it is 
		that strange compound object which is file and directory in the time). 
	    */
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Sorry, files are not supported yet!");
	    return -1;
	}

	track[aal_strlen(track)] = '/';
    }
    
    return 0;
}

/* This function opens object by its name */
reiserfs_object_t *reiserfs_object_open(
    reiserfs_fs_t *fs,		/* filesystem object (file/dir/else) will be opened on */
    const char *name		/* name of object (file/dir/else) */
) {
    reiserfs_key_t parent_key;
    reiserfs_object_t *object;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-789", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    reiserfs_key_init(&object->key, fs->key.plugin, fs->key.body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */

    reiserfs_key_init(&parent_key, fs->key.plugin, fs->key.body);
    
    if (reiserfs_object_lookup(object, name, &parent_key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find object \"%s\".", name);
	goto error_free_object;
    }
    
    /* Guessing object plugin from its first item */
    if (!(object->plugin = reiserfs_object_guess_plugin(object))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't guess object plugin.");
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    aal_free(object);
    return NULL;
}

#ifndef ENABLE_COMPACT

/* Creates new object on specified filesystem */
reiserfs_object_t *reiserfs_object_create(
    reiserfs_fs_t *fs,		    /* filesystem new object will be created on */
    reiserfs_plugin_t *plugin	    /* plugin to be used */
) {
    reiserfs_object_t *object;
    
    aal_assert("umka-790", fs != NULL, return NULL);
    aal_assert("umka-785", plugin != NULL, return NULL);
    
    /* Allocating the memory for obejct instance */
    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    /* Initializing fileds */
    object->fs = fs;
    object->plugin = plugin;

    return object;

error_free_object:
    aal_free(object);
    return NULL;
}

#endif

/* Closes specified object */
void reiserfs_object_close(
    reiserfs_object_t *object	    /* object to be closed */
) {
    aal_assert("umka-680", object != NULL, return);
    aal_free(object);
}

