/*
    plugin.c -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE
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

static int callback_match_plugin(reiserfs_plugin_t *plugin, struct walk_desc *desc) {
    return (plugin->h.type == desc->type && plugin->h.id == desc->id);
}

error_t reiserfs_plugins_init(void) {
#ifndef ENABLE_ALONE
    DIR *dir;
    struct dirent *ent;
#endif	

    aal_assert("umka-159", plugins == NULL, return -1);
    
#ifndef ENABLE_ALONE
    if (!(dir = opendir(PLUGIN_DIR))) {
    	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK,
	    "Can't open directory %s.", PLUGIN_DIR);
	return -1;
    }
	
    while ((ent = readdir(dir))) {
	char name[4096];
	reiserfs_plugin_t *plugin;

	if ((strlen(ent->d_name) == 1 && aal_strncmp(ent->d_name, ".", 1)) ||
		(strlen(ent->d_name) == 2 && aal_strncmp(ent->d_name, "..", 2)))
	    continue;	
	
	if (strlen(ent->d_name) <= 2)
	    continue;
		
	if (ent->d_name[strlen(ent->d_name) - 2] != 's' || 
		ent->d_name[strlen(ent->d_name) - 1] != 'o')
	    continue;
		
	aal_memset(name, 0, sizeof(name));
	aal_snprintf(name, sizeof(name), "%s/%s", PLUGIN_DIR, ent->d_name);
	
	if (!(plugin = reiserfs_plugins_load(name))) {
	    aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE,
		"Plugin %s was not loaded.", name);
	    continue;
	}
    }
    closedir(dir);
#else
    /* 
	Here must be initialization code for 
	builtin plugins. 
    */
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

reiserfs_plugin_t *reiserfs_plugins_find(reiserfs_plugin_type_t type, 
    reiserfs_plugin_id_t id) 
{
    struct walk_desc desc;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    desc.type = type;
    desc.id = id;
	
    if (!(plugin = (reiserfs_plugin_t *)aal_list_find_custom(plugins, (void *)&desc, 
	    (comp_func_t)callback_match_plugin)->data))
	return NULL;
	
    return plugin;
}

reiserfs_plugin_t *reiserfs_plugins_load(const char *name) {
#ifndef ENABLE_ALONE
    void *handle, *entry;
#endif
    reiserfs_plugin_t *plugin;
    
    aal_assert("umka-156", name != NULL, return NULL);
    
#ifndef ENABLE_ALONE
    if (!(handle = dlopen(name, RTLD_NOW))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't load plugin %s.", name);
	return NULL;
    }
    
    entry = dlsym(handle, PLUGIN_ENTRY);
    if (dlerror() != NULL || entry == NULL) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find symbol \"%s\" in plugin %s.", PLUGIN_ENTRY, name);
	goto error_free_handle;
    }
    
    plugin = *((reiserfs_plugin_t **)entry);
    plugin->h.handle = handle;
    
    plugins = aal_list_append(plugins, (void *)plugin);
	
    return plugin;
	
error_free_handle:
    dlclose(handle);
#else
    /* Here will be static plugins loading code */
#endif
error:
    return NULL;
}

void reiserfs_plugins_unload(reiserfs_plugin_t *plugin) {
    aal_assert("umka-158", plugin != NULL, return);
    aal_assert("umka-166", plugins != NULL, return);
#ifndef ENABLE_ALONE	
    if (!plugins)
	return;
	
    dlclose(plugin->h.handle);
#endif	
    aal_list_remove(plugins, plugin);
}

