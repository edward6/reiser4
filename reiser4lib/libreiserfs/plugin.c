/*
    plugin.c -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_COMPACT
#  include <dlfcn.h>
#  include <sys/types.h>
#  include <dirent.h>
#endif

#include <reiserfs/reiserfs.h>

aal_list_t *plugins = NULL;

struct walk_desc {
    reiserfs_plugin_type_t type;
    reiserfs_plugin_id_t id;
};

static int callback_match_coords(reiserfs_plugin_t *plugin, struct walk_desc *desc) {
    return (plugin->h.type == desc->type && plugin->h.id == desc->id);
}

static int callback_match_label(reiserfs_plugin_t *plugin, const char *label) {
    return aal_strncmp(plugin->h.label, label, aal_strlen(plugin->h.label));
}

#ifndef ENABLE_COMPACT

reiserfs_plugin_t *reiserfs_plugins_load_by_name(const char *name) {
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
	   "Can't find symbol \"%s\" in plugin %s.", "__plugin_entry", name);
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

reiserfs_plugin_t *reiserfs_plugins_load_by_entry(reiserfs_plugin_entry_t entry) {
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-259", entry != NULL, return NULL);
    
    if (!(plugin = entry())) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't initialiaze plugin.");
	return NULL;
    }
    
    /* Here will be some checks for plugin validness */
    
    plugins = aal_list_append(plugins, plugin);
    return plugin;
}

void reiserfs_plugins_unload(reiserfs_plugin_t *plugin) {
    aal_assert("umka-158", plugin != NULL, return);
    aal_assert("umka-166", plugins != NULL, return);
#ifndef ENABLE_COMPACT
    dlclose(plugin->h.handle);
#endif
    aal_list_remove(plugins, plugin);
}

error_t reiserfs_plugins_init(void) {
#ifndef ENABLE_COMPACT
    DIR *dir;
    struct dirent *ent;
#else
    uint32_t *entry;
    extern uint32_t __plugin_start;
    extern uint32_t __plugin_end;
#endif	

    aal_assert("umka-159", plugins == NULL, return -1);
    
#ifndef ENABLE_COMPACT
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
	reiserfs_plugins_load_by_name(name);
    }
    closedir(dir);
#else
    /* FIXME-umka: The following code is not 64-bit safe */
    for (entry = (uint32_t *)(&__plugin_start); entry < (uint32_t *)(&__plugin_end); entry++)
	reiserfs_plugins_load_entry((reiserfs_plugin_entry_t)*entry, NULL);
#endif
    return -(aal_list_length(plugins) == 0);
}

void reiserfs_plugins_fini(void) {
    aal_list_t *walk;

    for (walk = aal_list_last(plugins); walk; ) {
	aal_list_t *temp;
	
	temp = aal_list_prev(walk);
	reiserfs_plugins_unload((reiserfs_plugin_t *)walk->data);
	walk = temp;
    }	
    aal_list_free(plugins);
}

reiserfs_plugin_t *reiserfs_plugins_find_by_label(const char *label) {
    aal_list_t *found;

    aal_assert("umka-155", plugins != NULL, return NULL);
    aal_assert("umka-258", label != NULL, return NULL);
	
    return (found = aal_list_find_custom(aal_list_first(plugins), (void *)label, 
	(comp_func_t)callback_match_label)) ? (reiserfs_plugin_t *)found->data : NULL;
}

reiserfs_plugin_t *reiserfs_plugins_find_by_coords(reiserfs_plugin_type_t type, 
    reiserfs_plugin_id_t id) 
{
    aal_list_t *found;
    struct walk_desc desc;

    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    desc.type = type;
    desc.id = id;
	
    return (found = aal_list_find_custom(aal_list_first(plugins), (void *)&desc, 
	(comp_func_t)callback_match_coords)) ? (reiserfs_plugin_t *)found->data : NULL;
}

