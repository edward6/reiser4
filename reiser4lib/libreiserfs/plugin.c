/*
	plugin.c -- reiserfs plugin factory implementation.
	Copyright (C) 1996-2002 Hans Reiser
*/

#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <aal/aal.h>
#include <reiserfs/reiserfs.h>
#include <reiserfs/debug.h>

#if ENABLE_NLS
#  include <libintl.h>
#  define _(String) dgettext (PACKAGE, String)
#else
#  define _(String) (String)
#endif

#define REISERFS_PLUGIN_CASHE_SIZE 255

extern aal_list_t *plugin_cashe;
extern aal_list_t *plugin_map;

struct run_desc {
	reiserfs_plugin_type_t type;
	reiserfs_plugin_id_t id;
};

static int callback_match_plugin(reiserfs_plugin_t *plugin, struct run_desc *desc) {
	if (plugin->h.type == desc->type && plugin->h.id == desc->id)
		return 1;
	
	return 0;
}

static reiserfs_plugin_t *reiserfs_plugin_from_cashe(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id)
{
	struct run_desc desc;
	reiserfs_plugin_t *plugin;
	
	desc.type = type;
	desc.id = id;
	
	if (!(plugin = (reiserfs_plugin_t *)aal_list_run(plugin_cashe, 
			(int (*)(void *, void *))callback_match_plugin, (void *)&desc)))
		return NULL;
	
	plugin->h.nlink++;
	return plugin;
}

static void reiserfs_plugin_to_cashe(reiserfs_plugin_t *plugin) {
	plugin->h.nlink--;
}

reiserfs_plugin_t *reiserfs_plugin_load_by_name(const char *name, const char *point) {
	char *error;
	void *handle, *entry;
	reiserfs_plugin_t *plugin;
	reiserfs_plugin_t *(*get_plugin) (void);

	ASSERT(name != NULL, return NULL);
	ASSERT(point != NULL, return NULL);

	if (!(handle = dlopen(name, RTLD_NOW))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-001", 
			_("Can't load plugin library %s. Error: %s."), name, strerror(errno));
		return NULL;
	}

	entry = dlsym(handle, point);
	if ((error = dlerror()) != NULL) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-002", 
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

/* Looks for plugin by its coords in the plugin map. */
int reiserfs_plugin_find_by_cords(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id, char *name) 
{
	ASSERT(name != NULL, return 0);
	return 1;
}

reiserfs_plugin_t *reiserfs_plugin_load_by_cords(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id) 
{
	char name[PATH_MAX];
	reiserfs_plugin_t *plugin;
		
	/* Looking up code for plugin in plugin map must be here */
	if (!(plugin = reiserfs_plugin_from_cashe(type, id)))
		return plugin;
	
	/* Loading plugin */
	memset(name, 0, sizeof(name));
	if (!reiserfs_plugin_find_by_cords(type, id, name)) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-003", 
			_("Can't find plugin by its type=%d and id=%d."), (int)type, (int)id);
		return NULL;
	}

	if (!(plugin = reiserfs_plugin_load_by_name(name, "reiserfs_plugin_info"))) {
		aal_exception_throw(EXCEPTION_ERROR, EXCEPTION_OK, "umka-004", 
			_("Can't load plugin by name %s."), name);
		return NULL;
	}
	
	aal_list_add(plugin_cashe, (void *)plugin);
	
	return plugin;
}

void reiserfs_plugin_unload(reiserfs_plugin_t *plugin) {
	ASSERT(plugin != NULL, return);
	
	reiserfs_plugin_to_cashe(plugin);

	if (!plugin->h.nlink) {
		dlclose(plugin->h.handle);
		aal_list_remove(plugin_cashe, (void *)plugin);
	}	
}

