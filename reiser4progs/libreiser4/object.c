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
reiser4_plugin_t *reiser4_object_guess(reiser4_object_t *object) {
    reiser4_item_t item;
    
    if (reiser4_item_open(&item, object->coord.cache->node, 
	&object->coord.pos)) 
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't open item by coord. Node %llu, item %u.",
	    aal_block_number(object->coord.cache->node->block),
	    object->coord.pos.item);

	return NULL;
    }
    
    /* 
	FIXME-UMKA: Here should be used more carefull way to understand if we are
	able to call item_ops.specific.statdata method in order to get mode field
	from stat data item.
    */
    if (item.plugin->item_ops.specific.statdata.get_mode) {
	rid_t id, type;

	/* 
	    Guessing plugin type and plugin id by mode field from the stat data
	    item. This guessing should be performed only if stat data has not an
	    plugin extention plugin type and id might be got from.
	*/
	
	uint16_t mode = item.plugin->item_ops.specific.statdata.get_mode(item.body);
    
	if (S_ISDIR(mode)) {
	    type = DIR_PLUGIN_TYPE;
	    id = DIR_DIR40_ID;
	} else if (S_ISLNK(mode)) {
	    type = FILE_PLUGIN_TYPE;
	    id = FILE_SYMLINK40_ID;
	} else {
	    type = FILE_PLUGIN_TYPE;
	    id = FILE_REG40_ID;
	}
	
	return libreiser4_factory_ifind(type, id);
    }

    return NULL;
}

/* 
    Performs lookup of object statdata by object name. result of lookuping are 
    stored in passed object fileds. Returns error code or 0 if there is no errors.
    This function also supports symlinks and it rather might be called "stat", by
    means of work it performs.
*/
static errno_t reiser4_object_lookup(
    reiser4_object_t *object,	    /* object lookup will be performed for */
    const char *name,		    /* name to be parsed */
    reiser4_key_t *parent	    /* key of parent stat data */
) {
    reiser4_entity_t *entity;
    reiser4_plugin_t *plugin;

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
	reiser4_item_t item;

	/* FIXME-UMKA: Hardcoded key40 key type */
	reiser4_key_set_type(&object->key, KEY40_STATDATA_MINOR);
	reiser4_key_set_offset(&object->key, 0);
	
	if (reiser4_tree_lookup(object->fs->tree, REISER4_LEAF_LEVEL, 
	    &object->key, &object->coord) != 1) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory \"%s\".", track);
	    return -1;
	}
	
	if (reiser4_item_open(&item, object->coord.cache->node,
	    &object->coord.pos)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't open item by coord. Node %llu, item %u.",
		aal_block_number(object->coord.cache->node->block),
		object->coord.pos.item);

	    return -1;
	}
	
	/* 
	    FIXME-UMKA: Here should be used more carefull way to understand the
	    real item type (statdata, direntry, etc).
	*/
	if (item.plugin->item_ops.specific.statdata.get_mode) {
	    uint16_t mode;

	    /* 
		Checking for mode. It is used in order to know is current entry link or 
		not and is the mode valid one.
	    */
	    mode = item.plugin->item_ops.specific.statdata.get_mode(item.body);

	    if (!S_ISLNK(mode) && !S_ISDIR(mode) && !S_ISREG(mode)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "%s has invalid mode 0x%x.", track, mode);
		return -1;
	    }
		
	    if (S_ISLNK(mode)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Sorry, opening objects by link is not supported yet!");
		return -1;
	    }
	}

	/* It will be useful when symlinks ready */
	reiser4_key_set_locality(parent, reiser4_key_get_locality(&object->key));
	reiser4_key_set_objectid(parent, reiser4_key_get_objectid(&object->key));

	if (!(dirname = aal_strsep(&pointer, "/")))
	    break;
		
	if (!aal_strlen(dirname))
	    continue;
	
	aal_strncat(track, dirname, aal_strlen(dirname));
	
	/* 
	    Here we should get dir plugin id from the statdata and using it try find 
	    needed entry inside it.
	*/
	if (!(plugin = reiser4_object_guess(object))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't guess object plugin for parent of %s.", track);
	    return -1;
	}

	if (plugin->h.type == DIR_PLUGIN_TYPE) {
	    reiser4_entry_hint_t entry;
	    
	    if (!(entity = plugin_call(return -1, 
		plugin->dir_ops, open, object->fs->tree, &object->key)))
	    {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't open parent of directory \"%s\".", track);
		return -1;
	    }
	    
	    entry.name = dirname;
	    
	    if (!plugin->dir_ops.lookup) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Method \"lookup\" is not implemented in %s plugin.", 
		    plugin->h.label);
		
		plugin_call(return -1, plugin->dir_ops, 
		    close, entity);
		
		return -1;
	    }
	
	    if (plugin->dir_ops.lookup(entity, &entry)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		    "Can't find entry \"%s\".", entry.name);
		
		plugin_call(return -1, plugin->dir_ops, 
		    close, entity);
		
		return -1;
	    }
	    
	    plugin_call(return -1, plugin->dir_ops, 
		close, entity);

	    /* Updating object key by found objectid and locality */
	    reiser4_key_set_objectid(&object->key, entry.objid.objectid);
	    reiser4_key_set_locality(&object->key, entry.objid.locality);
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
reiser4_object_t *reiser4_object_open(
    reiser4_fs_t *fs,		/* filesystem object (file/dir/else) will be opened on */
    const char *name		/* name of object (file/dir/else) */
) {
    reiser4_key_t *root_key;
    reiser4_key_t parent_key;
    reiser4_object_t *object;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-789", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;
    
    object->fs = fs;

    root_key = &fs->tree->key;
    
    reiser4_key_init(&object->key, root_key->plugin, root_key->body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */
    reiser4_key_init(&parent_key, root_key->plugin, root_key->body);
    
    if (reiser4_object_lookup(object, name, &parent_key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find object \"%s\".", name);
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    aal_free(object);
    return NULL;
}

#ifndef ENABLE_COMPACT

/* Creates new object on specified filesystem */
reiser4_object_t *reiser4_object_create(
    reiser4_fs_t *fs,		    /* filesystem new object will be created on */
    reiser4_plugin_t *plugin	    /* plugin to be used */
) {
    reiser4_object_t *object;
    
    aal_assert("umka-790", fs != NULL, return NULL);
    aal_assert("umka-785", plugin != NULL, return NULL);
    
    /* Allocating the memory for obejct instance */
    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    /* Initializing fileds */
    object->fs = fs;

    return object;

error_free_object:
    aal_free(object);
    return NULL;
}

#endif

/* Closes specified object */
void reiser4_object_close(
    reiser4_object_t *object	    /* object to be closed */
) {
    aal_assert("umka-680", object != NULL, return);
    aal_free(object);
}

