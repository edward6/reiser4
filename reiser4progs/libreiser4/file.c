/*
    file.c -- common code for files and directories.
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
reiser4_plugin_t *reiser4_file_guess(reiser4_file_t *file) {
    reiser4_item_t item;
    
    if (reiser4_item_open(&item, file->coord.cache->node, 
	&file->coord.pos)) 
    {
	aal_exception_error("Can't open item by coord. Node %llu, item %u.",
	    aal_block_number(file->coord.cache->node->block),
	    file->coord.pos.item);

	return NULL;
    }
    
    if (reiser4_item_statdata(&item)) {
	/* 
	    FIXME-UMKA: Here should be inspecting of the stat data extentions
	    in order to find out is there some plugin id extention exists and
	    if so, what the plugin id should be used for working with this kind
	    of file.
	*/
	    
	/* 
	    Guessing plugin type and plugin id by mode field from the stat data 
	    item. Here we return default plugins for every file type.
	*/
	uint16_t mode = reiser4_item_get_smode(&item);
    
	if (S_ISDIR(mode))
	    return libreiser4_factory_ifind(DIR_PLUGIN_TYPE, DIR_DIR40_ID);
	
	if (S_ISLNK(mode))
	    return libreiser4_factory_ifind(FILE_PLUGIN_TYPE, FILE_SYMLINK40_ID);
	
	return libreiser4_factory_ifind(FILE_PLUGIN_TYPE, FILE_REGULAR40_ID);
    }

    return NULL;
}

/* 
    Performs lookup of file statdata by its name. result of lookuping are stored 
    in passed object fileds. Returns error code or 0 if there is no errors. This 
    function also supports symlinks and it rather might be called "stat", by means 
    of work it performs.
*/
static errno_t reiser4_file_realize(
    reiser4_file_t *file,	    /* file lookup will be performed in */
    const char *name,		    /* name to be parsed */
    reiser4_key_t *parent	    /* key of parent stat data */
) {
    reiser4_entity_t *entity;
    reiser4_plugin_t *plugin;

    char track[4096], path[4096];
    char *pointer = NULL, *dirname = NULL;

    aal_assert("umka-682", file != NULL, return -1);
    aal_assert("umka-681", name != NULL, return -1);
    aal_assert("umka-685", parent != NULL, return -1);
    
    aal_memset(path, 0, sizeof(path));
    aal_memset(track, 0, sizeof(track));
    
    aal_strncpy(path, name, sizeof(path));
    
    if (path[0] != '.' || path[0] == '/')
	track[aal_strlen(track)] = '/';
  
    pointer = path[0] == '/' ? &path[1] : &path[0];

    /* Main big loop all work is performed inside wich */
    while (1) {
	reiser4_item_t item;

	reiser4_key_set_type(&file->key, KEY_STATDATA_TYPE);
	reiser4_key_set_offset(&file->key, 0);
	
	if (reiser4_tree_lookup(file->fs->tree, REISER4_LEAF_LEVEL, 
	    &file->key, &file->coord) != 1) 
	{
	    aal_exception_error("Can't find stat data of directory \"%s\".", track);
	    return -1;
	}
	
	if (reiser4_item_open(&item, file->coord.cache->node,
	    &file->coord.pos)) 
	{
	    aal_exception_error("Can't open item by coord. Node %llu, item %u.",
		aal_block_number(file->coord.cache->node->block),
		file->coord.pos.item);

	    return -1;
	}
	
	/* 
	    FIXME-UMKA: Here probably should be opening of file and then calling
	    the method "follow" for following the link if it is a link.
	*/
	if (reiser4_item_statdata(&item)) {
	    uint16_t mode;

	    /* 
		Checking for mode. It is used in order to know is current entry link or 
		not and is the mode valid one.
	    */
	    mode = reiser4_item_get_smode(&item);

	    if (!S_ISLNK(mode) && !S_ISDIR(mode) && !S_ISREG(mode)) {
		aal_exception_error("%s has invalid mode 0x%x.", track, mode);
		return -1;
	    }
		
	    if (S_ISLNK(mode)) {
		aal_exception_error("Sorry, opening the files by link is "
		    "not supported yet!");
		return -1;
	    }
	}

	/* It will be useful when symlinks ready */
	reiser4_key_set_locality(parent, 
	    reiser4_key_get_locality(&file->key));
	
	reiser4_key_set_objectid(parent, 
	    reiser4_key_get_objectid(&file->key));

	if (!(dirname = aal_strsep(&pointer, "/")))
	    break;
		
	if (!aal_strlen(dirname))
	    continue;
	
	aal_strncat(track, dirname, aal_strlen(dirname));
	
	if (!(plugin = reiser4_file_guess(file))) {
	    aal_exception_error("Can't guess file plugin for "
		"parent of %s.", track);
	    return -1;
	}

	if (!(entity = plugin_call(return -1, plugin->file_ops, open, 
	    file->fs->tree, &file->key)))
	{
	    aal_exception_error("Can't open parent of directory \"%s\".", track);
	    return -1;
	}
	    
	if (!plugin->file_ops.lookup) {
	    aal_exception_error("Method \"lookup\" is not implemented in %s plugin.", 
	        plugin->h.label);
		
	    plugin_call(return -1, plugin->file_ops, close, entity);
	    return -1;
	}
	
	if (plugin->file_ops.lookup(entity, dirname, &file->key) != 1) {
	    aal_exception_error("Can't find entry \"%s\".", dirname);
		
	    plugin_call(return -1, plugin->file_ops, close, entity);
	    return -1;
	}
	    
       	plugin_call(return -1, plugin->file_ops, close, entity);
	track[aal_strlen(track)] = '/';
    }
    
    return 0;
}

/* This function opens file by its name */
reiser4_file_t *reiser4_file_open(
    reiser4_fs_t *fs,		/* filesystem object (file/dir/else) will be opened on */
    const char *name		/* name of file to be opened */
) {
    reiser4_key_t *root_key;
    reiser4_key_t parent_key;
    reiser4_file_t *file;
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-789", name != NULL, return NULL);

    if (!(file = aal_calloc(sizeof(*file), 0)))
	return NULL;
    
    file->fs = fs;

    root_key = &fs->tree->key;
    reiser4_key_init(&file->key, root_key->plugin, root_key->body);
    
    /* 
	I assume that name is absolute name. So, user, who will call this method 
	should convert name previously into absolute one by getcwd function.
    */
    reiser4_key_init(&parent_key, root_key->plugin, root_key->body);
    
    if (reiser4_file_realize(file, name, &parent_key)) {
	aal_exception_error("Can't find file \"%s\".", name);
	goto error_free_file;
    }
    
    if (!(plugin = reiser4_file_guess(file))) {
	aal_exception_error("Can't find file plugin in "
	    "order to open %s.", name);
	
	goto error_free_file;
    }
    
    if (!(file->entity = plugin_call(goto error_free_file, 
        plugin->file_ops, open, fs->tree, &file->key)))
    {
        aal_exception_error("Can't open %s.", name);
	goto error_free_file;
    }
    
    return file;
    
error_free_file:
    aal_free(file);
    return NULL;
}

#ifndef ENABLE_COMPACT

/* Adds speficied entry into passed opened dir */
errno_t reiser4_file_add(
    reiser4_file_t *file,	    /* dir new entry will be add in */
    reiser4_entry_hint_t *hint	    /* new entry hint */
) {
    aal_assert("umka-862", file != NULL, return -1);
    aal_assert("umka-863", file->entity != NULL, return -1);
    
    return plugin_call(return -1, file->entity->plugin->file_ops, 
        add, file->entity, hint);
}

/* Creates new file on specified filesystem */
reiser4_file_t *reiser4_file_create(
    reiser4_fs_t *fs,		    /* filesystem dir will be created on */
    reiser4_file_hint_t *hint,    /* directory hint */
    reiser4_plugin_t *plugin,	    /* plugin to be used */
    reiser4_file_t *parent,	    /* parent file */
    const char *name		    /* name of entry */
) {
    reiser4_file_t *file;
    
    roid_t objectid, locality;
    reiser4_key_t parent_key, file_key;
    
    aal_assert("umka-790", fs != NULL, return NULL);
    aal_assert("umka-1128", hint != NULL, return NULL);
    aal_assert("umka-1150", plugin != NULL, return NULL);
    aal_assert("umka-1152", name != NULL, return NULL);
    
    /* Allocating the memory for obejct instance */
    if (!(file = aal_calloc(sizeof(*file), 0)))
	return NULL;

    /* Initializing fileds and preparing keys */
    file->fs = fs;
    
    /* 
	This is a special case. In the case parent is NULL, we are trying to
	create root directory.
    */
    if (parent) {
        reiser4_key_init(&parent_key, parent->key.plugin, parent->key.body);
        objectid = reiser4_oid_allocate(parent->fs->oid);
    } else {
	roid_t root_locality = reiser4_oid_root_locality(fs->oid);
	roid_t root_parent_locality = reiser4_oid_root_parent_locality(fs->oid);
		
        parent_key.plugin = fs->tree->key.plugin;
        reiser4_key_build_generic(&parent_key, KEY_STATDATA_TYPE, 
	    root_parent_locality, root_locality, 0);

	objectid = reiser4_oid_root_objectid(fs->oid);
    }

    locality = reiser4_key_get_objectid(&parent_key);
    
    /* Building stat data key of directory */
    file_key.plugin = parent_key.plugin;
    reiser4_key_build_generic(&file_key, KEY_STATDATA_TYPE,
        locality, objectid, 0);
    
    reiser4_key_init(&file->key, file_key.plugin, file_key.body);
    
    /* Creating entry in parent */
    if (parent) {   
	reiser4_entry_hint_t entry;

	/* 
	    Creating entry in parent directory. It should be done first, because
	    if such directory exist we preffer just return error and do not delete
	    inserted file stat data and some kind of body.
	*/
	aal_memset(&entry, 0, sizeof(entry));
	
	entry.objid.objectid = reiser4_key_get_objectid(&file->key);
	entry.objid.locality = reiser4_key_get_locality(&file->key);
	entry.name = (char *)name;

	if (reiser4_file_add(parent, &entry)) {
	    aal_exception_error("Can't add entry \"%s\".", name);
	    goto error_free_file;
	}
    }

    if (!(file->entity = plugin_call(goto error_free_file, 
	plugin->file_ops, create, fs->tree, &parent_key, &file_key, hint)))
    {
	aal_exception_error("Can't create file with oid 0x%llx.", 
	    reiser4_key_get_objectid(&file_key));
	goto error_free_file;
    }
    
    return file;

error_free_file:
    aal_free(file);
    return NULL;
}

#endif

/* Closes specified file */
void reiser4_file_close(
    reiser4_file_t *file	    /* file to be closed */
) {
    aal_assert("umka-680", file != NULL, return);
    aal_assert("umka-1149", file->entity != NULL, return);

    plugin_call(goto error_free_file, file->entity->plugin->file_ops,
	close, file->entity);
    
error_free_file:
    aal_free(file);
}

/* Resets directory position */
errno_t reiser4_file_reset(
    reiser4_file_t *file	    /* dir to be rewinded */
) {
    aal_assert("umka-842", file != NULL, return -1);
    aal_assert("umka-843", file->entity != NULL, return -1);

    return plugin_call(return -1, file->entity->plugin->file_ops, 
	reset, file->entity);
}

/* Reads one entry from directory, current position points on */
errno_t reiser4_file_entry(
    reiser4_file_t *file,	    /* dir entry will be read from */
    reiser4_entry_hint_t *hint	    /* entry pointer result will be stored in */
) {
    aal_assert("umka-860", file != NULL, return -1);
    aal_assert("umka-861", file->entity != NULL, return -1);

    return plugin_call(return -1, file->entity->plugin->file_ops, 
        entry, file->entity, hint);
}

/* Retutns current position in directory */
uint32_t reiser4_file_offset(
    reiser4_file_t *file	    /* dir position will be obtained from */
) {
    aal_assert("umka-875", file != NULL, return -1);
    aal_assert("umka-876", file->entity != NULL, return -1);

    return plugin_call(return -1, file->entity->plugin->file_ops, 
	offset, file->entity);
}

/* Seeks directory current position to passed pos */
errno_t reiser4_file_seek(
    reiser4_file_t *file,	    /* dir where position shopuld be chnaged */
    uint32_t offset		    /* offset for seeking */
) {
    aal_assert("umka-1129", file != NULL, return -1);
    aal_assert("umka-1153", file->entity != NULL, return -1);
    
    return plugin_call(return -1, file->entity->plugin->file_ops, 
	seek, file->entity, offset);
}

