/*
    plugin.c -- reiserfs plugin factory implementation.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifndef ENABLE_ALONE
#  include <dlfcn.h>
#endif

#include <reiserfs/reiserfs.h>

extern aal_list_t *plugins;

struct walk_desc {
    reiserfs_plugin_type_t type;
    reiserfs_plugin_id_t id;
};

static int callback_match_cashe_plugin(reiserfs_plugin_t *plugin, struct walk_desc *desc) {
    if (plugin->h.type == desc->type && plugin->h.id == desc->id)
	return 1;
	
    return 0;
}

reiserfs_plugin_t *reiserfs_plugin_find(reiserfs_plugin_type_t type, 
    reiserfs_plugin_id_t id) 
{
    struct walk_desc desc;
    reiserfs_plugin_t *plugin;

    aal_assert("umka-155", plugins != NULL, return NULL);    
	
    desc.type = type;
    desc.id = id;
	
    if (!(plugin = (reiserfs_plugin_t *)aal_list_run(plugins, 
	    (int (*)(void *, void *))callback_match_cashe_plugin, (void *)&desc)))
	return NULL;
	
    return plugin;
}

reiserfs_plugin_t *reiserfs_plugin_load(const char *filename) {
#ifndef ENABLE_ALONE
    char *error;
    void *handle, *entry;
    reiserfs_plugin_t *plugin;
	
    aal_assert("umka-156", filename != NULL, return NULL);
    aal_assert("umka-157", plugins != NULL, return NULL); 
	
    if (!(handle = dlopen(filename, RTLD_NOW))) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK,
	    "Can't load plugin %s.", filename);
	return NULL;
    }
    
    entry = dlsym(handle, PLUGIN_ENTRY);
    if ((error = dlerror()) != NULL || entry == NULL) {
	aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, 
	    "Can't find symbol \"%s\" in plugin %s.", PLUGIN_ENTRY, filename);
	goto error_free_handle;
    }
    
    plugin = *((reiserfs_plugin_t **)entry);
    plugin->h.handle = handle;
    aal_list_add(plugins, (void *)plugin);
	
    return plugin;
	
error_free_handle:
    dlclose(handle);
error:
    return NULL;
#else
    return NULL;
#endif
}

void reiserfs_plugin_unload(reiserfs_plugin_t *plugin) {
    aal_assert("umka-158", plugin != NULL, return);
#ifndef ENABLE_ALONE	
    if (!plugins)
	return;
	
    dlclose(plugin->h.handle);
#endif	
    aal_list_remove(plugins, (void *)plugin);
}

