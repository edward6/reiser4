/*
    plugin.c -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
#  include <dlfcn.h>
#  include <sys/types.h>
#  include <dirent.h>
#endif

#include <reiser4/reiser4.h>

struct walk_desc {
    reiserfs_plugin_type_t type;
    reiserfs_id_t id;
};

typedef struct walk_desc walk_desc_t;

aal_list_t *plugins = NULL;

extern reiserfs_core_t core;

static int callback_match_coord(reiserfs_plugin_t *plugin, walk_desc_t *desc) {
    return (plugin->h.type == desc->type && plugin->h.id == desc->id);
}

#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)

reiserfs_plugin_t *libreiser4_plugin_load_by_name(const char *name) {
    void *handle, *addr;
    reiserfs_plugin_t *plugin;
    reiserfs_plugin_entry_t entry;

    aal_assert("umka-260", name != NULL, return NULL);
    
    if (!(handle = dlopen(name, RTLD_NOW))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	   "Can't load plugin %s.", name);
	return NULL;
    }

    addr = dlsym(handle, "__plugin_entry");
    if (dlerror() != NULL || entry == NULL) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't find entry point in plugin %s.", name);
	goto error_free_handle;
    }
    
    entry = *((reiserfs_plugin_entry_t *)addr);
    if (!(plugin = reiserfs_plugins_load_by_entry(entry)))
	goto error_free_handle;
    
    plugin->h.handle = handle;
    return plugin;
    
error_free_handle:    
    dlclose(handle);
error:
    return NULL;
}

#endif

reiserfs_plugin_t *libreiser4_plugin_load_by_entry(reiserfs_plugin_entry_t entry) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-259", entry != NULL, return NULL);
    
    if (!(plugin = entry(&core))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialiaze plugin.");
	return NULL;
    }
    
    /* Here will be some checks for plugin validness */
    
    plugins = aal_list_append(plugins, plugin);
    return plugin;
}

void libreiser4_plugin_unload(reiserfs_plugin_t *plugin) {
    aal_assert("umka-158", plugin != NULL, return);
    aal_assert("umka-166", plugins != NULL, return);
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    dlclose(plugin->h.handle);
#endif
    aal_list_remove(plugins, plugin);
}

errno_t libreiser4_factory_init(void) {
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    DIR *dir;
    struct dirent *ent;
#else
    uint32_t *entry;
    extern uint32_t __plugin_start;
    extern uint32_t __plugin_end;
#endif	

    aal_assert("umka-159", plugins == NULL, return -1);
    
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    if (!(dir = opendir(PLUGIN_DIR))) {
    	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't open directory %s.", PLUGIN_DIR);
	return -1;
    }
	
    while ((ent = readdir(dir))) {
	char name[256];

	if ((aal_strlen(ent->d_name) == 1 && aal_strncmp(ent->d_name, ".", 1)) ||
		(aal_strlen(ent->d_name) == 2 && aal_strncmp(ent->d_name, "..", 2)))
	    continue;	
	
	if (aal_strlen(ent->d_name) <= 2)
	    continue;
		
	if (ent->d_name[aal_strlen(ent->d_name) - 2] != 's' || 
		ent->d_name[aal_strlen(ent->d_name) - 1] != 'o')
	    continue;
		
	aal_memset(name, 0, sizeof(name));
	aal_snprintf(name, sizeof(name), "%s/%s", PLUGIN_DIR, ent->d_name);
	libreiser4_plugins_load_by_name(name);
    }
    closedir(dir);
#else
    /* FIXME-UMKA: The following code is not 64-bit safe */
    for (entry = (uint32_t *)(&__plugin_start) + 1; 
	entry < (uint32_t *)(&__plugin_end); entry++) 
    {
	if (entry) 
	    libreiser4_plugin_load_by_entry((reiserfs_plugin_entry_t)*entry);
    }
#endif
    return -(aal_list_length(plugins) == 0);
}

void libreiser4_factory_done(void) {
    aal_list_t *walk;

    aal_assert("umka-335", plugins != NULL, return);
    
    for (walk = aal_list_last(plugins); walk; ) {
	aal_list_t *temp = aal_list_prev(walk);
	libreiser4_plugin_unload((reiserfs_plugin_t *)walk->item);
	walk = temp;
    }
    plugins = NULL;
}

reiserfs_plugin_t *libreiser4_factory_find(reiserfs_plugin_type_t type, reiserfs_id_t id) {
    aal_list_t *found;
    walk_desc_t desc;

    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    desc.type = type;
    desc.id = id;
	
    return (found = aal_list_find_custom(aal_list_first(plugins), (void *)&desc, 
	(comp_func_t)callback_match_coord, NULL)) ? (reiserfs_plugin_t *)found->item : NULL;
}

errno_t libreiser4_plugins_foreach(reiserfs_plugin_func_t plugin_func, void *data) {
    errno_t res = 0;
    aal_list_t *walk;
    
    aal_assert("umka-479", plugin_func != NULL, return -1);

    aal_list_foreach_forward(walk, plugins) {
	reiserfs_plugin_t *plugin = (reiserfs_plugin_t *)walk->item;
	
	if ((res = plugin_func(plugin, data)))
	    return res;
    }
    return res;
}

