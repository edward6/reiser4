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
reiser4_object_t *reiser4_dir_open(
    reiser4_fs_t *fs,		    /* filesystem instance dir will be opened on */
    const char *name		    /* name of directory to be opened */
) {
    reiser4_object_t *object;
    reiser4_plugin_t *plugin;
    
    /* Initializes object and finds stat data */
    if (!(object = reiser4_object_open(fs, name)))
	return NULL;
    
    if (!(plugin = reiser4_object_guess(object))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find object plugin in order to open %s.", name);
	goto error_free_object;
    }
    
    if (!(object->entity = plugin_call(goto error_free_object, 
        plugin->dir_ops, open, fs->tree, &object->key)))
    {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't open directory %s.", name);
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    reiser4_object_close(object);
    return object;
}

/* Closes passed reiserfs object (directory in this case) */
void reiser4_dir_close(
    reiser4_object_t *object	    /* directory to be closed */
) {
    aal_assert("umka-680", object != NULL, return);
    aal_assert("umka-841", object->entity != NULL, return);

    plugin_call(goto error_free_object, 
	object->entity->plugin->dir_ops, close, object->entity);

error_free_object:
    reiser4_object_close(object);
}

#ifndef ENABLE_COMPACT

/* 
    Creates directory on specified filesystem of specified kind dictated by
    passed plugin, hint in speficied by name place.
*/
reiser4_object_t *reiser4_dir_create(
    reiser4_fs_t *fs,		    /* filesystem dir will be created on */
    reiser4_object_hint_t *hint,    /* directory hint */
    reiser4_plugin_t *plugin,	    /* plugin to be used */
    reiser4_object_t *parent,	    /* parent object */
    const char *name		    /* name of entry */
) {
    roid_t objectid, locality;
    reiser4_key_t parent_key, object_key;
    
    reiser4_object_t *object;

    if (!(object = reiser4_object_create(fs)))
	return NULL;
    
    /* 
	This is a special case. In the case parent is NULL, we are trying to
	create root directory.
    */
    if (parent) {
        reiser4_key_init(&parent_key, parent->key.plugin, parent->key.body);
        objectid = reiser4_oid_alloc(parent->fs->oid);
    } else {
	roid_t root_locality = reiser4_oid_root_locality(fs->oid);
	roid_t root_parent_locality = reiser4_oid_root_parent_locality(fs->oid);
		
        parent_key.plugin = fs->tree->key.plugin;
        reiser4_key_build_generic(&parent_key, KEY_STATDATA_TYPE, 
	    root_parent_locality, root_locality, 0);

	objectid = reiser4_oid_root_objectid(fs->oid);
    }

    locality = reiser4_key_get_objectid(&parent_key);
    
    object_key.plugin = parent_key.plugin;

    /* Building stat data key of directory */
    reiser4_key_build_generic(&object_key, KEY_STATDATA_TYPE,
        locality, objectid, 0);
    
    /* Updating object key */
    {
	uint32_t key_size = plugin_call(goto error_free_object, 
	    object_key.plugin->key_ops, size,);
	
	reiser4_key_init(&object->key, object_key.plugin, object_key.body);
    }
    
    if (parent) {   
	reiser4_entry_hint_t entry;

	/* 
	    Creating entry in parent directory. It shouldbe done first, because
	    if such directory exist we preffer just return error and do not delete
	    inserted object stat data and some kind of body.
	*/
	aal_memset(&entry, 0, sizeof(entry));
	
	entry.objid.objectid = reiser4_key_get_objectid(&object->key);
	entry.objid.locality = reiser4_key_get_locality(&object->key);
	entry.name = (char *)name;

	if (reiser4_dir_add(parent, &entry)) {
	    aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
		"Can't add entry \"%s\".", name);
	    goto error_free_object;
	}
    }

    if (!(object->entity = plugin_call(goto error_free_object, 
	plugin->dir_ops, create, fs->tree, &parent_key, &object_key, hint)))
    {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "Can't create object"
	    " with oid 0x%llx.", reiser4_key_get_objectid(&object_key));
	goto error_free_object;
    }
    
    return object;
    
error_free_object:
    reiser4_object_close(object);
    return NULL;
}

/* Adds speficied entry into passed opened dir */
errno_t reiser4_dir_add(
    reiser4_object_t *object,	    /* dir new entry will be add in */
    reiser4_entry_hint_t *hint	    /* new entry hint */
) {
    aal_assert("umka-862", object != NULL, return -1);
    aal_assert("umka-863", object->entity != NULL, return -1);
    
    return plugin_call(return -1, object->entity->plugin->dir_ops, 
        add, object->entity, hint);
}

#endif

/* Resets directory position */
errno_t reiser4_dir_rewind(
    reiser4_object_t *object	    /* dir to be rewinded */
) {
    aal_assert("umka-842", object != NULL, return -1);
    aal_assert("umka-843", object->entity != NULL, return -1);

    return plugin_call(return -1, object->entity->plugin->dir_ops, 
	rewind, object->entity);
}

/* Reads one entry from directory, current position points on */
errno_t reiser4_dir_read(
    reiser4_object_t *object,	    /* dir entry will be read from */
    reiser4_entry_hint_t *hint	    /* entry pointer result will be stored in */
) {
    aal_assert("umka-860", object != NULL, return -1);
    aal_assert("umka-861", object->entity != NULL, return -1);

    return plugin_call(return -1, object->entity->plugin->dir_ops, 
        read, object->entity, hint);
}

/* Retutns current position in directory */
uint32_t reiser4_dir_tell(
    reiser4_object_t *object	    /* dir position will be obtained from */
) {
    aal_assert("umka-875", object != NULL, return -1);
    aal_assert("umka-876", object->entity != NULL, return -1);

    return plugin_call(return -1, object->entity->plugin->dir_ops, 
	tell, object->entity);
}

