/*
    object.c -- common code for files and directories.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

static int reiserfs_object_find_entry(reiserfs_coord_t *coord, 
    uint64_t hash) 
{
    return 0;
}
	
static error_t reiserfs_object_find_stat(reiserfs_object_t *object, const char *name, 
    reiserfs_key_t *parent) 
{
    uint64_t hash;
    reiserfs_plugin_t key_plugin;
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
	uint16_t mode;

	/* FIXME-UMKA: Hardcoded key40 key type */
	reiserfs_key_set_type(&object->key, KEY40_STATDATA_MINOR);
	reiserfs_key_set_offset(&object->key, 0);
	
	if (!reiserfs_tree_lookup(fs, &object->key, &object->coord)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}
	
	/* Checking whether found item is a link */
	mode = *((uint16_t *)reiserfs_node_item_at(object->coord.block, 
	    object->coord.item_pos));
		
	if (!S_ISLNK(LE16_TO_CPU(*mode)) && !S_ISDIR(LE16_TO_CPU(*mode)) && 
	    !S_ISREG(LE16_TO_CPU(*mode))) 
	{
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"%s has invalid object type."), track;
	    return -1;
	}
		
	if (S_ISLNK(LE16_TO_CPU(*mode))) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Sorry, opening objects by link is not supported yet!");
	    return -1;
	}

	/* It will be useful when symlinks ready */
	reiserfs_key_set_locality(&parent, reiserfs_key_get_locality(&object->key));
	reiserfs_key_set_objectid(&parent, reiserfs_key_get_objectid(&object->key));

	if (!(dirname = aal_strsep(&pointer, '/')))
	    break;
		
	if (!aal_strlen(dirname))
	    continue;
		
	aal_strncat(track, dirname, aal_strlen(dirname));
	
	/* 
	    FIXME-UMKA: Here will be calling of the hash plugin
	    when ready.
	*/
	hash = 0;

	/* FIXME-UMKA: Hardcoded key40 key type */
	reiserfs_key_set_type(&object->key, KEY40_FILENAME_MINOR);
	reiserfs_key_set_hash(&object->key, hash);
	
	if (!reiserfs_tree_lookup(fs, &object->key, &object->coord)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find stat data of directory %s.", track);
	    return -1;
	}

	if (!reiserfs_object_find_entry(&object->coord, hash)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't find entry %s.", track);
	    return -1;
	}

	track[strlen(track)] = '/';
    }
    
    return 0;
}

static void reiserfs_object_absname(const char *name, char *absname, uint16_t n) {

    aal_assert("umka-683", name != NULL, return);
    aal_assert("umka-684", absname != NULL, return);
    
    if (name[0] != '/') {
	aal_memset(absname, 0, n);
	getcwd(absname, n);
	aal_strncat(absname, "/", 1);
	aal_strncat(absname, name, n - aal_strlen(absname));
    } else
        aal_strncpy(absname, name, n);
}

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, const char *name) {
    char absname[4096];
    reiserfs_key_t parent
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
    
    reiserfs_key_init(&parent, key_plugin);
    reiserfs_key_clean(&parent);
    
    reiserfs_key_build_file_key(&parent, KEY40_STATDATA_MINOR, reiserfs_oid_root_parent_objectid(fs), 
	reiserfs_oid_root_objectid(fs), 0);
    
    aal_memcpy(&object->key, &parent, sizeof(parent));
    
    reiserfs_object_absname(name, absname, sizeof(4096));
    
    if (reiserfs_object_find_stat(object, absname, &parent)) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find %s.", name);
	return NULL;
    }

    return object;
}

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    aal_free(object);
}

