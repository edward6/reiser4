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

static int reiserfs_object_find_entry(reiserfs_coord_t *coord, reiserfs_key_t *key) {
    return 0;
}
	
static error_t reiserfs_object_lookup(reiserfs_object_t *object, const char *name, 
    reiserfs_key_t *parent) 
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
	
	if (!reiserfs_tree_lookup(object->fs->tree, &object->key, &object->coord)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}
	
	/* Checking whether found item is a link */
	if (!(body = reiserfs_node_item_at(object->coord.node, 
	    object->coord.pos.item_pos))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item body. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.node->block), 
		object->coord.pos.item_pos);
	    return -1;
	}
	
	if (!(plugin = reiserfs_node_item_get_plugin(object->coord.node, 
	    object->coord.pos.item_pos)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item plugin. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.node->block),
		object->coord.pos.item_pos);
	    return -1;
	}
	
	mode = libreiser4_plugin_call(return -1, plugin->item.specific.stat, get_mode, body);

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
	    FIXME-UMKA: Hardcoded key40 key type should be fixed. Also 
	    key id should be recived from anywhere. And finally, hash_plugin
	    should not be initializing every time.
	*/
	if (!(hash_plugin = libreiser4_factory_find_by_coord(REISERFS_HASH_PLUGIN, 0x0)))
	    libreiser4_factory_find_failed(REISERFS_HASH_PLUGIN, 0x0, return -1);
	
	reiserfs_key_build_dir_key(&object->key, hash_plugin, 
	    reiserfs_key_get_locality(&object->key), 
	    reiserfs_key_get_objectid(&object->key), dirname);
	
	if (!reiserfs_tree_lookup(object->fs->tree, &object->key, &object->coord)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}

	if (!reiserfs_object_find_entry(&object->coord, &object->key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find entry %s.", track);
	    return -1;
	}

	track[strlen(track)] = '/';
    }
    
    return 0;
}

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, const char *name) {
    reiserfs_key_t *parent;
    reiserfs_object_t *object;
    reiserfs_plugin_t *key_plugin;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-679", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    
    /* FIXME-UMKA: Hardcoded key plugin id */
    if (!(key_plugin = libreiser4_factory_find_by_coord(REISERFS_KEY_PLUGIN, 0x0)))
	libreiser4_factory_find_failed(REISERFS_KEY_PLUGIN, 0x0, return NULL);
   
    parent = reiserfs_oid_root_key(fs->oid); 
    object->key = *parent;;
    
    /* 
	I assume that name is absolute name. So, user, who will 
	call this method should convert name previously into absolute
	one by getcwd function.
    */
    if (reiserfs_object_lookup(object, name, parent)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find %s.", name);
	return NULL;
    }

    /*
	FIXME-UMKA: It will be call of corresponding object plugin here
	for initialization object-type-specific things.
    */
//    object->entity = libreiser4_plugin_call();

    
    return object;
}

#ifndef ENABLE_COMPACT

reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, 
    reiserfs_coord_t *coord, reiserfs_profile_t *profile)
{
    reiserfs_opaque_t *dir;
    reiserfs_object_t *object;
    reiserfs_plugin_t *dir_plugin;
    
    aal_assert("umka-740", fs != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;
    
    if (!(dir_plugin = libreiser4_factory_find_by_coord(REISERFS_DIR_PLUGIN, 
	profile->dir)))
    {
    	libreiser4_factory_find_failed(REISERFS_DIR_PLUGIN, profile->dir,
	    goto error_free_object);
    }

    if (!(dir = libreiser4_plugin_call(goto error_free_object, dir_plugin->dir, 
	create, coord->node->block, coord->pos.item_pos, profile->key, 
	profile->item.statdata, profile->item.direntry, profile->oid, profile->node)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't create directory.");
	goto error_free_object;
    }
    
    libreiser4_plugin_call(goto error_free_object, dir_plugin->dir, close, dir);
    
    return object;

error_free_object:
    aal_free(object);
    return NULL;
}

#endif

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    aal_free(object);
}

