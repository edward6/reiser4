/*
    factory.c -- reiser4 plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* There is possible to use dlopen-ed plugins */
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
#  include <dlfcn.h>
#  include <dirent.h>
#  include <errno.h>
#  include <sys/types.h>
#endif

#include <reiser4/reiser4.h>

/* Helper structure used in searching of plugins */
struct walk_desc {
    rpid_t id;			    /* needed plugin id */
    rpid_t type;			    /* needed plugin type */
    const char *name;
};

typedef struct walk_desc walk_desc_t;

/* This list contain all known libreiser4 plugins */
aal_list_t *plugins = NULL;

extern reiser4_core_t core;

static int callback_match_coord(reiser4_plugin_t *plugin, walk_desc_t *desc) {
    return (plugin->h.type == desc->type);
}

/* Helper callback function for matching plugin by type and id */
static int callback_match_id(
    reiser4_plugin_t *plugin,	    /* current plugin in list */
    walk_desc_t *desc		    /* desction contained needed plugin type and id */
) {
    return (plugin->h.type == desc->type 
	&& plugin->h.id == desc->id);
}

static int callback_match_name(reiser4_plugin_t *plugin, walk_desc_t *desc) {
    return (plugin->h.type == desc->type 
	&& !aal_strncmp(plugin->h.label, desc->name, aal_strlen(desc->name)));
}
    
/* Loads plugin by entry point (used for builtin plugins) */
reiser4_plugin_t *libreiser4_plugin_eload(reiser4_plugin_entry_t entry) {
    reiser4_plugin_t *plugin;
    
    aal_assert("umka-259", entry != NULL, return NULL);
    
    if (!(plugin = entry(&core))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialiaze plugin.");
	return NULL;
    }
    
    /* Here will be some checks for plugin validness */
    
    /* Registering plugin in plugins list */
    plugins = aal_list_append(plugins, plugin);
    return plugin;
}

/* Loads non-builtin plugin by filename */
reiser4_plugin_t *libreiser4_plugin_fload(const char *name) {
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    void *handle, *addr;
    reiser4_plugin_t *plugin;
    reiser4_plugin_entry_t entry;

    aal_assert("umka-260", name != NULL, return NULL);
    
    /* Loading specified plugin filename */
    if (!(handle = dlopen(name, RTLD_NOW))) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	   "Can't load plugin \"%s\". %s.", name, dlerror());
	return NULL;
    }

    /* Getting plugin entry point */
    addr = dlsym(handle, "__plugin_entry");
    if (dlerror() != NULL || addr == NULL) {
        aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	   "Can't find entry point in plugin \"%s\". %s.", 
	   name, dlerror());
	goto error_free_handle;
    }
    
    /* Getting plugin info by entry point */
    entry = *((reiser4_plugin_entry_t *)addr);
    if (!(plugin = libreiser4_plugin_eload(entry)))
	goto error_free_handle;
    
    plugin->h.handle = handle;
    return plugin;
    
error_free_handle:    
    dlclose(handle);
error:
    return NULL;
#else
    return NULL;
#endif
}

/* Upload specified plugin */
void libreiser4_plugin_uload(reiser4_plugin_t *plugin) {
    aal_assert("umka-158", plugin != NULL, return);
    aal_assert("umka-166", plugins != NULL, return);
    
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    dlclose(plugin->h.handle);
#endif
    aal_list_remove(plugins, plugin);
}

/* Initializes plugin factory by means of loading all available plugins */
errno_t libreiser4_factory_init(void) {
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)
    DIR *dir;
    struct dirent *ent;
#else
    unsigned long *entry;
    extern unsigned long __plugin_start;
    extern unsigned long __plugin_end;
#endif	

    aal_assert("umka-159", plugins == NULL, return -1);
    
#if !defined(ENABLE_COMPACT) && !defined(ENABLE_MONOLITHIC)

    /* Loads all dynamic loadable plugins */
    if (!(dir = opendir(PLUGIN_DIR))) {
    	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't open directory %s.", PLUGIN_DIR);
	return -1;
    }
	
    /* Getting plugins filenames */
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

	/* Loading plugin*/
	libreiser4_plugin_load_name(name);
    }
    closedir(dir);
#else
    /* Loads the all builtin plugins */
    for (entry = &__plugin_start + 1; entry < &__plugin_end; entry++) {
	if (!entry) continue;
        libreiser4_plugin_eload((reiser4_plugin_entry_t)*entry);
    }
#endif
    return -(aal_list_length(plugins) == 0);
}

/* Finalizes plugin factory, by means of unloading the all plugins */
void libreiser4_factory_done(void) {
    aal_list_t *walk;

    aal_assert("umka-335", plugins != NULL, return);
    
    /* Unloading all registered plugins */
    for (walk = aal_list_last(plugins); walk; ) {
	aal_list_t *temp = aal_list_prev(walk);
	libreiser4_plugin_uload((reiser4_plugin_t *)walk->item);
	walk = temp;
    }
    plugins = NULL;
}

/* Finds plugins by its type and id */
reiser4_plugin_t *libreiser4_factory_ifind(
    rpid_t type,			    /* requested plugin type */
    rpid_t id				    /* requested plugin id */
) {
    aal_list_t *found;
    walk_desc_t desc;

    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    desc.type = type;
    desc.id = id;
	
    /* Calling list function in order to find needed plugin */
    return (found = aal_list_find_custom(aal_list_first(plugins), (void *)&desc, 
       (comp_func_t)callback_match_id, NULL)) ? (reiser4_plugin_t *)found->item : NULL;
}

/* Makes search for plugin by name */
reiser4_plugin_t *libreiser4_factory_nfind(
    rpid_t type,			    /* needed plugin type */
    const char *name			    /* needed plugin name */
) {
    aal_list_t *found;
    walk_desc_t desc;

    aal_assert("vpf-156", name != NULL, return NULL);    
       
    desc.type = type;
    desc.name = name;
       
    return (found = aal_list_find_custom(aal_list_first(plugins), (void *)&desc, 
	(comp_func_t)callback_match_name, NULL)) ? (reiser4_plugin_t *)found->item : NULL;
}

/* Finds plugins by its type and id */
reiser4_plugin_t *libreiser4_factory_cfind(
    reiser4_plugin_func_t func,	    /* per plugin function */
    void *data				    /* user-specified data */
) {
    aal_list_t *walk = NULL;

    aal_assert("umka-899", func != NULL, return NULL);    
    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    aal_list_foreach_forward(walk, plugins) {
	reiser4_plugin_t *plugin = (reiser4_plugin_t *)walk->item;
	
	if (func(plugin, data))
	    return plugin;
    }
    
    return NULL;
}

/* 
    Calls specified function for every plugin from plugin list. This functions
    is used for getting any plugins information.
*/
errno_t libreiser4_factory_foreach(
    reiser4_plugin_func_t func,	    /* per plugin function */
    void *data				    /* user-specified data */
) {
    errno_t res = 0;
    aal_list_t *walk;
    
    aal_assert("umka-479", func != NULL, return -1);

    aal_list_foreach_forward(walk, plugins) {
	reiser4_plugin_t *plugin = (reiser4_plugin_t *)walk->item;
	
	if ((res = func(plugin, data)))
	    return res;
    }
    return res;
}

