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

static int reiserfs_object_find_entry(reiserfs_coord_t *coord, 
    reiserfs_key_t *key) 
{
    return 0;
}
	
static errno_t reiserfs_object_lookup(reiserfs_object_t *object, 
    const char *name, reiserfs_key_t *parent) 
{
    reiserfs_plugin_t *key_plugin;
    reiserfs_plugin_t *hash_plugin;
    
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
	
    pointer = &path[0];
    while (1) {
	void *body; uint16_t mode;
	reiserfs_plugin_t *plugin;

	/* FIXME-UMKA: Hardcoded key40 key type */
	reiserfs_key_set_type(&object->key, KEY40_STATDATA_MINOR);
	reiserfs_key_set_offset(&object->key, 0);
	
	if (!reiserfs_tree_lookup(object->fs->tree, REISERFS_LEAF_LEVEL, 
	    &object->key, &object->coord)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}
	
	/* Checking whether found item is a link */
	if (!(body = reiserfs_node_item_body(object->coord.cache->node, 
	    object->coord.pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item body. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.cache->node->block), 
		object->coord.pos.item);
	    return -1;
	}
	
	if (!(plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	    object->coord.pos.item)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item plugin. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.cache->node->block),
		object->coord.pos.item);
	    return -1;
	}
	
	mode = libreiser4_plugin_call(return -1, 
	    plugin->item_ops.specific.statdata, get_mode, body);

	if (!S_ISLNK(LE16_TO_CPU(mode)) && !S_ISDIR(LE16_TO_CPU(mode)) && 
	    !S_ISREG(LE16_TO_CPU(mode))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"%s has invalid object type.", track);
	    return -1;
	}
		
	if (S_ISLNK(LE16_TO_CPU(mode))) {
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
	    FIXME-UMKA: Hardcoded key40 key type should be fixed. 
	    Also key id should be recived from anywhere. And finally, 
	    hash_plugin should not be initializing every time.
	*/
	if (!(hash_plugin = libreiser4_factory_find(REISERFS_HASH_PLUGIN, 0x0)))
	    libreiser4_factory_failed(return -1, find, hash, 0x0);
	
	reiserfs_key_build_entry_full(&object->key, hash_plugin, 
	    reiserfs_key_get_locality(&object->key), 
	    reiserfs_key_get_objectid(&object->key), 
	    dirname);
	
	if (!reiserfs_tree_lookup(object->fs->tree, REISERFS_LEAF_LEVEL, 
	    &object->key, &object->coord)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}

	if (!reiserfs_object_find_entry(&object->coord, &object->key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find entry %s.", track);
	    return -1;
	}

	track[aal_strlen(track)] = '/';
    }
    
    return 0;
}

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, 
    reiserfs_plugin_t *plugin, const char *name) 
{
    reiserfs_key_t parent_key;
    reiserfs_object_t *object;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-789", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    object->plugin = plugin;

    object->key.plugin = fs->key.plugin;
    reiserfs_key_init(&object->key, fs->key.body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */

    parent_key.plugin = fs->key.plugin;
    reiserfs_key_init(&parent_key, fs->key.body);
    
    if (reiserfs_object_lookup(object, name, &parent_key)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find %s.", name);
	return NULL;
    }
    
    if (plugin->h.type == REISERFS_DIR_PLUGIN) {
	if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
	    object->plugin->dir_ops, open, fs->tree, &object->key)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't open directory %s.", name);
	    goto error_free_object;
	}
    } else {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, files are not supported yet!");
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    aal_free(object);
    return NULL;
}

#ifndef ENABLE_COMPACT

reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, reiserfs_plugin_t *plugin, 
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

    object->key.plugin = fs->key.plugin;
    reiserfs_key_init(&object->key, fs->key.body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */
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
	
    if (plugin->h.type == REISERFS_DIR_PLUGIN) {
	if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
	    plugin->dir_ops, create, fs->tree, &parent_key, &object_key)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create object with oid %llx.", reiserfs_key_get_objectid(&object_key));
	    goto error_free_object;
	}
    } else {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, files are not supported now!");
	goto error_free_object;
    }
    
    /* Here will be also adding entry to parent object */
    
    object->key = object_key;
    object->plugin = plugin;
    
    return object;

error_free_object:
    aal_free(object);
    return NULL;
}

#endif

errno_t reiserfs_object_rewind(reiserfs_object_t *object) {
    aal_assert("umka-842", object != NULL, return -1);
    aal_assert("umka-843", object->entity != NULL, return -1);

    if (object->plugin->h.type == REISERFS_DIR_PLUGIN) {
	return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	    rewind, object->entity);
    } else {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, files are not supported now!");
	return -1;
    }
}

errno_t reiserfs_object_read(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint) 
{
    aal_assert("umka-860", object != NULL, return -1);
    aal_assert("umka-861", object->entity != NULL, return -1);

    if (object->plugin->h.type == REISERFS_DIR_PLUGIN) {
	return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	    read, object->entity, hint);
    } else {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, files are not supported now!");
	return -1;
    }
}

errno_t reiserfs_object_add(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint) 
{
    aal_assert("umka-862", object != NULL, return -1);
    aal_assert("umka-863", object->entity != NULL, return -1);

    if (object->plugin->h.type == REISERFS_DIR_PLUGIN) {
	return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	    add, object->entity, hint);
    } else {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Sorry, files are not supported now!");
	return -1;
    }
}

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    aal_assert("umka-841", object->entity != NULL, return);
    
    libreiser4_plugin_call(return, object->plugin->dir_ops, 
        close, object->entity);
    
    aal_free(object);
}

