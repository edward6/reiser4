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

/* Translation table for item types. It is used for nice exceptions */
char *reiserfs_item_name[] = {
    [REISERFS_STATDATA_ITEM] = "STATDATA",
    [REISERFS_DIRENTRY_ITEM] = "DIRENTRY",
    [REISERFS_INTERNAL_ITEM] = "INTERNAL",
    [REISERFS_FILENTRY_ITEM] = "FILEENTRY"
};

static int reiserfs_object_find_entry(reiserfs_coord_t *coord, reiserfs_key_t *key) {
    return 0;
}
	
static errno_t reiserfs_object_lookup(reiserfs_object_t *object, const char *name, 
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
	
	if (!reiserfs_tree_lookup(object->fs->tree, REISERFS_LEAF_LEVEL, 
	    &object->key, &object->coord)) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}
	
	/* Checking whether found item is a link */
	if (!(body = reiserfs_node_item_body(object->coord.node, 
	    object->coord.pos.item))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item body. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.node->block), 
		object->coord.pos.item);
	    return -1;
	}
	
	if (!(plugin = reiserfs_node_get_item_plugin(object->coord.node, 
	    object->coord.pos.item)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't get item plugin. Node %llu, item %u.", 
		aal_block_get_nr(object->coord.node->block),
		object->coord.pos.item);
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
	    FIXME-UMKA: Hardcoded key40 key type should be fixed. 
	    Also key id should be recived from anywhere. And finally, 
	    hash_plugin should not be initializing every time.
	*/
	if (!(hash_plugin = libreiser4_factory_find(REISERFS_HASH_PLUGIN, 0x0)))
	    libreiser4_factory_failed(return -1, find, hash, 0x0);
	
	reiserfs_key_build_dir_key(&object->key, hash_plugin, 
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

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, const char *name) {
    reiserfs_key_t parent_key;
    reiserfs_object_t *object;
    
    aal_assert("umka-678", fs != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    object->key = fs->key;
    
    /* FIXME */
//    object->plugin = 
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */
    parent_key = fs->key;
    
    if (name) {
	if (reiserfs_object_lookup(object, name, &parent_key)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find %s.", name);
	    return NULL;
	}
    }

    return object;
}

#ifndef ENABLE_COMPACT

reiserfs_object_t *reiserfs_object_create(reiserfs_fs_t *fs, 
    const char *name, reiserfs_plugin_t *plugin, reiserfs_profile_t *profile)
{
    int i;
    reiserfs_object_t *object;
    oid_t objectid, parent_objectid;
    reiserfs_key_t parent_key, object_key;
    
    aal_assert("umka-784", fs != NULL, return NULL);
    aal_assert("umka-785", plugin != NULL, return NULL);
    aal_assert("umka-786", profile != NULL, return NULL);
    
    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    object->fs = fs;
    object->key = fs->key;
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */
    if (name) {
	char *ptr;
	char parent_name[256];
	reiserfs_object_t *parent;
	
	if (!(ptr = aal_strrchr(name, '/'))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Invalid name %s.", name);
	    goto error_free_object;
	}
	
	aal_memset(parent_name, 0, sizeof(parent_name));
	aal_strncpy(parent_name, name, ptr - name);
	
	if (!(parent = reiserfs_object_open(fs, parent_name)))
	    goto error_free_object;
	
	parent_key = parent->key;
	objectid = reiserfs_oid_alloc(fs->oid);

	reiserfs_object_close(parent);
    } else {
	parent_key.plugin = fs->key.plugin;
	reiserfs_key_build_file_key(&parent_key, KEY40_STATDATA_MINOR, 
	    reiserfs_oid_root_parent_locality(fs->oid), 
	    reiserfs_oid_root_parent_objectid(fs->oid), 0);

	objectid = reiserfs_oid_root_objectid(fs->oid);
    }
    parent_objectid = reiserfs_key_get_objectid(&parent_key);
    
    object_key.plugin = parent_key.plugin;
    reiserfs_key_build_file_key(&object_key, KEY40_STATDATA_MINOR,
	parent_objectid, objectid, 0);
	
    if (plugin->h.type == REISERFS_DIR_PLUGIN) {
	if (!(object->hint = libreiser4_plugin_call(goto error_free_object, plugin->dir, 
	    build, &parent_key, &object_key, profile->item.statdata, profile->item.direntry)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create directory hint.");
	    goto error_free_object;
	}
    } else {
	if (!(object->hint = libreiser4_plugin_call(goto error_free_object, plugin->file, 
	    build, &parent_key, &object_key, profile->item.statdata, profile->item.fileentry)))
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't create file hint.");
	    goto error_free_object;
	}
    }
    
    if (object->hint->count == 0) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Empty object hint has been received.");
	goto error_free_object;
    }
    
    /* Inserting all items into tree */
    for (i = 0; i < object->hint->count; i++) {
	if (reiserfs_tree_insert(fs->tree, &object->hint->item[i])) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't insert \"%s\" item of object %llx into the tree.", 
		reiserfs_item_name[object->hint->item[i].type], 
		reiserfs_key_get_objectid(&object_key));
	    goto error_free_hint;
	}
    }

    object->key = object_key;
    object->plugin = plugin;
    
    return object;

error_free_hint:
    libreiser4_plugin_call(goto error_free_object, plugin->dir, 
	destroy, object->hint);
error_free_object:
    aal_free(object);
    return NULL;
}

#endif

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    
    libreiser4_plugin_call(return, object->plugin->dir, 
	destroy, object->hint);

    aal_free(object);
}

