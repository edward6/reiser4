/*
	plugin.c -- reiserfs plugin factory implementation.
	Copyright (C) 1996 - 2002 Hans Reiser
*/

#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <reiserfs/plugin.h>
#include <reiserfs/debug.h>

reiserfs_plugin_t *reiserfs_plugin_load_by_name(const char *name, const char *point) {
	char *error;
	void *handle, *entry;
	reiserfs_plugin_t *plugin;
	reiserfs_plugin_t *(get_plugin) (void);

	ASSERT(name != NULL, return NULL);
	ASSERT(point != NULL, return NULL);

	if (!(handle = dlopen(name, RTLD_NOW))) {
		reiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-0001", 
			_("Can't load plugin library %s. Error: %s."), name, strerror(errno));
		return NULL;
	}

	entry = dlsym(handle, point);
	if ((error = dlerror()) != NULL) {
		reiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-0002", 
			_("Can't find symbol %s in plugin %s. Error: %s."), point, name, error);
		goto error_free_handle;
	}
	
	get_plugin = (reiserfs_plugin_t *(*)(void))entry;
	plugin = get_plugin();

	plugin->h.handle = handle;
	plugin->h.nlink = 0;

	return plugin;
	
error_free_handle:
	dlclose(handle);
error:
	return NULL;
}

static reiserfs_plugin_t *reiserfs_plugin_from_cashe(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id) 
{
	reiserfs_plugin_t *plugin;
	
	/* Getting plugin from plugin cashe */
	
	plugin->h.nlink++;
	return NULL;
}

static void reiserfs_plugin_to_cashe(reiserfs_plugin_t plugin) {
	plugin->h.nlink--;
	if (!plugin->h.nlink) {
		/* Delete specified plugin from plugin cashe */
	}
}

int reiserfs_plugin_find_by_cords(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id, char *name) 
{
	ASSERT(name != NULL, return 0);
	return 1;
}

reiserfs_plugin_t *reiserfs_plugin_load(reiserfs_plugin_type_t type, reiserfs_plugin_id_t id) {
	char name[PATH_MAX];
	reiserfs_plugin_t *plugin;
		
	/* Looking up code for plugin in plugin map must be here */
	if (!(plugin = reiserfs_plugin_from_cashe(type, id)))
		return plugin;
	
	/* Loading plugin */
	memset(name, 0, sizeof(name));
	if (!reiserfs_plugin_find_by_cords(type, id, name)) {
		reiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-0003", 
			_("Can't find plugin by its type=%d and id=%d."), (int)type, (int)id);
		return NULL;
	}

	if (!(plugin = reiserfs_plugin_load_by_name(name))) {
		reiserfs_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-0004", 
			_("Can't load plugin by name %s."), name);
		return NULL;
	}
	
	return plugin;
}

void reiserfs_plugin_unload(reiserfs_plugin_t *plugin) {
	ASSERT(plugin != NULL, return);
	reiserfs_plugin_to_cashe(plugin);
	if (!plugin->h.nlink)
		dlclose(plugin);
}

