/*
	plugin.c -- reiserfs plugin factory implementation.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifndef ENABLE_ALONE
#  include <dlfcn.h>
#endif

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

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
	
	if (!plugins) 
		return NULL;
	
	desc.type = type;
	desc.id = id;
	
	if (!(plugin = (reiserfs_plugin_t *)aal_list_run(plugins, 
			(int (*)(void *, void *))callback_match_cashe_plugin, (void *)&desc)))
		return NULL;
	
	return plugin;
}

reiserfs_plugin_t *reiserfs_plugin_load(const char *name, const char *point) {
#ifndef ENABLE_ALONE
	char *error;
	void *handle, *entry;
	reiserfs_plugin_t *plugin;
	reiserfs_plugin_t *(*get_plugin) (void);

	ASSERT(name != NULL, return NULL);
	ASSERT(point != NULL, return NULL);

	if (!plugins) 
		return NULL;
	
	if (!(handle = dlopen(name, RTLD_NOW))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-001", 
			"Can't load plugin library %s.", name);
		return NULL;
	}

	entry = dlsym(handle, point);
	if ((error = dlerror()) != NULL) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-002", 
			"Can't find symbol %s in plugin %s. %s.", point, name, error);
		goto error_free_handle;
	}
	
	get_plugin = (reiserfs_plugin_t *(*)(void))entry;
	plugin = get_plugin();

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
#ifndef ENABLE_ALONE	
	ASSERT(plugin != NULL, return);

	if (!plugins)
		return;
	
	dlclose(plugin->h.handle);
#endif	
	aal_list_remove(plugins, (void *)plugin);
}

