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
    possible that pased item body is stat data body.
*/
static reiserfs_plugin_t *__guess_object_plugin(reiserfs_plugin_t *plugin, 
    void *body)
{
    /* FIXME-UMKA: Here should be real detecting instead of hardcoded plugin */
    return libreiser4_factory_find_by_id(REISERFS_OBJECT_PLUGIN, REISERFS_DIR40_ID);
}

static errno_t reiserfs_object_lookup(reiserfs_object_t *object, 
    const char *name, reiserfs_key_t *parent) 
{
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
	    aal_throw_error(EO_OK, "Can't find stat data of directory \"%s\".\n", track);
	    return -1;
	}
	
	/* Checking whether found item is a link */
	if (!(item_body = reiserfs_node_item_body(object->coord.cache->node, 
	    object->coord.pos.item))) 
	{
	    aal_throw_error(EO_OK, "Can't get item body. Node %llu, item %u.\n", 
		aal_block_get_nr(object->coord.cache->node->block), 
		object->coord.pos.item);
	    return -1;
	}
	
	if (!(item_plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	    object->coord.pos.item)))
	{
	    aal_throw_error(EO_OK, "Can't get item plugin. Node %llu, item %u.\n", 
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
	    aal_throw_error(EO_OK, "%s has invalid object type.\n", track);
	    return -1;
	}
		
	if (S_ISLNK(mode)) {
	    aal_throw_error(EO_OK, "Sorry, opening objects by link is not supported yet!\n");
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
	if (!(object_plugin = __guess_object_plugin(item_plugin, item_body))) {
	    aal_throw_error(EO_OK, "Can't guess object plugin for parent of %s.\n", track);
	    return -1;
	}

	if (!object_plugin->dir_ops.lookup) {
	    aal_throw_error(EO_OK, "Method \"lookup\" is not implemented in %s plugin.\n", 
		object_plugin->h.label);
	    return -1;
	}
	
	if (object_plugin->h.type == REISERFS_OBJECT_PLUGIN) {
	    reiserfs_entry_hint_t entry;
	    
	    if (!(object_entity = libreiser4_plugin_call(return -1, 
		object_plugin->dir_ops, open, object->fs->tree, &object->key)))
	    {
		aal_throw_error(EO_OK, "Can't open parent of directory \"%s\".\n", track);
		return -1;
	    }
	    
	    entry.name = dirname;
	    if (object_plugin->dir_ops.lookup(object_entity, &entry)) {
		aal_throw_error(EO_OK, "Can't find entry \"%s\".\n", entry.name);
		
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
	    aal_throw_error(EO_OK, "Sorry, files are not supported yet!\n");
	    return -1;
	}

	track[aal_strlen(track)] = '/';
    }
    
    return 0;
}

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, 
    const char *name) 
{
    reiserfs_key_t parent_key;
    reiserfs_object_t *object;
    
    reiserfs_plugin_t *item_plugin;
    void *item_body;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-789", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;

    object->key.plugin = fs->key.plugin;
    reiserfs_key_init(&object->key, fs->key.body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */

    parent_key.plugin = fs->key.plugin;
    reiserfs_key_init(&parent_key, fs->key.body);
    
    if (reiserfs_object_lookup(object, name, &parent_key)) {
	aal_throw_error(EO_OK, "Can't find object \"%s\".\n", name);
	goto error_free_object;
    }
    
    if (!(item_plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_throw_error(EO_OK, "Can't find first item plugin.\n");
	goto error_free_object;
    }
    
    if (!(item_body = reiserfs_node_item_body(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_throw_error(EO_OK, "Can't find first item plugin.\n");
	goto error_free_object;
    }
    
    if (!(object->plugin = __guess_object_plugin(item_plugin, item_body))) {
	aal_throw_error(EO_OK, "Can't guess object plugin.\n");
	goto error_free_object;
    }
    
    if (object->plugin->h.type == REISERFS_OBJECT_PLUGIN) {
	if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
	    object->plugin->dir_ops, open, fs->tree, &object->key)))
	{
	    aal_throw_error(EO_OK, "Can't open directory %s.\n", name);
	    goto error_free_object;
	}
    } else {
	aal_throw_error(EO_OK, "Sorry, files are not supported yet!\n");
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    aal_free(object);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, 
    reiserfs_object_hint_t *hint, reiserfs_plugin_t *plugin, 
    reiserfs_object_t *parent, const char *name)
{
    int i;
    reiserfs_object_t *object;
    oid_t objectid, parent_objectid;
    reiserfs_key_t parent_key, object_key;
    
    aal_assert("umka-790", fs != NULL, return NULL);
    aal_assert("umka-785", plugin != NULL, return NULL);
    
    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    object->plugin = plugin;

    object->key.plugin = fs->key.plugin;
    reiserfs_key_init(&object->key, fs->key.body);
    
    if (parent) {
	parent_key.plugin = parent->key.plugin;
	reiserfs_key_init(&parent_key, parent->key.body);
	objectid = reiserfs_oid_alloc(parent->fs->oid);
    } else {
	parent_key.plugin = fs->key.plugin;
	reiserfs_key_build_generic_full(&parent_key, KEY40_STATDATA_MINOR, 
	    reiserfs_oid_root_parent_locality(fs->oid), 
	    reiserfs_oid_root_parent_objectid(fs->oid), 0);

	objectid = reiserfs_oid_root_objectid(fs->oid);
    }
    parent_objectid = reiserfs_key_get_objectid(&parent_key);
    
    object_key.plugin = parent_key.plugin;
    reiserfs_key_build_generic_full(&object_key, KEY40_STATDATA_MINOR,
	parent_objectid, objectid, 0);
    
    if (object->plugin->h.type == REISERFS_OBJECT_PLUGIN) {
	if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
	    plugin->dir_ops, create, fs->tree, &parent_key, &object_key, hint)))
	{
	    aal_throw_error(EO_OK, "Can't create object with oid %llx.\n", 
		reiserfs_key_get_objectid(&object_key));
	    goto error_free_object;
	}
    } else {
	aal_throw_error(EO_OK, "Sorry, files are not supported now!\n");
	goto error_free_object;
    }
    
    /* Here will be also adding entry to parent object */

    object->key.plugin = object_key.plugin;

    aal_memcpy(object->key.body, object_key.body, 
	libreiser4_plugin_call(goto error_free_object, 
	object_key.plugin->key_ops, size,));
    
    return object;

error_free_object:
    aal_free(object);
    return NULL;
}

#endif

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    aal_assert("umka-841", object->entity != NULL, return);
    
    libreiser4_plugin_call(return, object->plugin->dir_ops, 
        close, object->entity);
    
    aal_free(object);
}

