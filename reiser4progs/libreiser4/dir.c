/*
    dir.c -- directory specific code. It uses common object code, that stored
    in object.c. See it for details.
    
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

/* This function opens specified dir on specified opened filesystem */
reiserfs_object_t *reiserfs_dir_open(
    reiserfs_fs_t *fs,		    /* filesystem instance dir will be opened on */
    const char *name		    /* name of directory to be opened */
) {
    void *item_body;
    reiserfs_plugin_t *item_plugin;

    reiserfs_object_t *object;
    
    /* Initializes object and finds stat data */
    if (!(object = reiserfs_object_open(fs, name)))
	return NULL;
    
    /* Getting plugin for the first object item (most probably stat data item) */
    if (!(item_plugin = reiserfs_node_item_get_plugin(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find first item plugin.");
	goto error_free_object;
    }
    
    /* Getting first item body */
    if (!(item_body = reiserfs_node_item_body(object->coord.cache->node, 
	object->coord.pos.item)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find first item plugin.");
	goto error_free_object;
    }
    
    if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
        object->plugin->dir_ops, open, fs->tree, &object->key)))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't open directory %s.", name);
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    reiserfs_object_close(object);
    return object;
}

/* Closes passed reiserfs object (directory in this case) */
void reiserfs_dir_close(
    reiserfs_object_t *object	    /* directory to be closed */
) {
    aal_assert("umka-680", object != NULL, return);
    aal_assert("umka-841", object->entity != NULL, return);

    libreiser4_plugin_call(goto error_free_object, 
	object->plugin->dir_ops, close, object->entity);

error_free_object:
    reiserfs_object_close(object);
}

#ifndef ENABLE_COMPACT

/* 
    Creates directory on specified filesystem of specified kind dictated by
    passed plugin, hint in speficied by name place.
*/
reiserfs_object_t *reiserfs_dir_create(
    reiserfs_fs_t *fs,		    /* filesystem dir will be created on */
    reiserfs_object_hint_t *hint,   /* directory hint */
    reiserfs_plugin_t *plugin,	    /* plugin to be used */
    reiserfs_object_t *parent,	    /* parent object */
    const char *name		    /* name of entry */
) {
    oid_t objectid, parent_objectid;
    reiserfs_key_t parent_key, object_key;
    
    reiserfs_object_t *object;

    if (!(object = reiserfs_object_create(fs, plugin)))
	return NULL;
    
    if (parent) {
        reiserfs_key_init(&parent_key, parent->key.plugin, parent->key.body);
        objectid = reiserfs_oid_alloc(parent->fs->oid);
    } else {
	oid_t root_locality = reiserfs_oid_root_locality(fs->oid);
	oid_t root_parent_locality = reiserfs_oid_root_parent_locality(fs->oid);
		
        parent_key.plugin = fs->key.plugin;
        reiserfs_key_build_generic_full(&parent_key, 
	    KEY40_STATDATA_MINOR, root_parent_locality, 
	    root_locality, 0);

	objectid = reiserfs_oid_root_objectid(fs->oid);
    }
    parent_objectid = reiserfs_key_get_objectid(&parent_key);
    
    object_key.plugin = parent_key.plugin;
    reiserfs_key_build_generic_full(&object_key, KEY40_STATDATA_MINOR,
        parent_objectid, objectid, 0);
    
    /* Updating object key */
    {
	uint32_t key_size = libreiser4_plugin_call(goto error_free_object, 
	    object_key.plugin->key_ops, size,);
	
	object->key.plugin = object_key.plugin;
	aal_memcpy(object->key.body, object_key.body, key_size);
    }
    
    if (parent) {   
	reiserfs_entry_hint_t entry;

	/* 
	    Creating entry in parent directory. It shouldbe done first, because
	    if such directory exist we preffer just return error and do not delete
	    inserted object stat data and some kind of body.
	*/
	aal_memset(&entry, 0, sizeof(entry));
	
	entry.objid.objectid = reiserfs_key_get_objectid(&object->key);
	entry.objid.locality = reiserfs_key_get_locality(&object->key);
	entry.name = (char *)name;

	if (reiserfs_dir_add(parent, &entry)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't add entry \"%s\".", name);
	    goto error_free_object;
	}
    }

    if (!(object->entity = libreiser4_plugin_call(goto error_free_object, 
	plugin->dir_ops, create, fs->tree, &parent_key, &object_key, hint)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create object"
	    " with oid %llx.", reiserfs_key_get_objectid(&object_key));
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    reiserfs_object_close(object);
    return NULL;
}

/* Adds speficied entry into passed opened dir */
errno_t reiserfs_dir_add(
    reiserfs_object_t *object,	    /* dir new entry will be add in */
    reiserfs_entry_hint_t *hint	    /* new entry hint */
) {
    aal_assert("umka-862", object != NULL, return -1);
    aal_assert("umka-863", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
        add, object->entity, hint);
}

#endif

/* Resets directory position */
errno_t reiserfs_dir_rewind(
    reiserfs_object_t *object	    /* dir to be rewinded */
) {
    aal_assert("umka-842", object != NULL, return -1);
    aal_assert("umka-843", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	rewind, object->entity);
}

/* Reads one entry from directory, current position points on */
errno_t reiserfs_dir_read(
    reiserfs_object_t *object,	    /* dir entry will be read from */
    reiserfs_entry_hint_t *hint	    /* entry pointer result will be stored in */
) {
    aal_assert("umka-860", object != NULL, return -1);
    aal_assert("umka-861", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
        read, object->entity, hint);
}

/* Retutns current position in directory */
uint32_t reiserfs_dir_tell(
    reiserfs_object_t *object	    /* dir position will be obtained from */
) {
    aal_assert("umka-875", object != NULL, return -1);
    aal_assert("umka-876", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	tell, object->entity);
}

