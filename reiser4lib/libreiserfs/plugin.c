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

extern aal_list_t *plugin_cashe;
extern aal_list_t *plugin_map;

struct walk_desc {
	reiserfs_plugin_type_t type;
	reiserfs_plugin_id_t id;
};

static int callback_match_cashe_plugin(reiserfs_plugin_t *plugin, struct walk_desc *desc) {
	
	if (plugin->h.type == desc->type && plugin->h.id == desc->id)
		return 1;
	
	return 0;
}

static int callback_match_map_plugin(reiserfs_item_t *item, struct walk_desc *desc) {
	
	if (item->type == desc->type && item->id == desc->id)
		return 1;
	
	return 0;
}

static reiserfs_plugin_t *reiserfs_plugin_find_in_cashe(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id)
{
	struct walk_desc desc;
	reiserfs_plugin_t *plugin;
	
	desc.type = type;
	desc.id = id;
	
	if (!(plugin = (reiserfs_plugin_t *)aal_list_run(plugin_cashe, 
			(int (*)(void *, void *))callback_match_cashe_plugin, (void *)&desc)))
		return NULL;
	
	return plugin;
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
static char *reiserfs_plugin_find_in_map(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id) 
{
	struct walk_desc desc;
	reiserfs_item_t *item;
	
	desc.type = type;
	desc.id = id;
	
	if (!(item = (reiserfs_item_t *)aal_list_run(plugin_map, 
			(int (*)(void *, void *))callback_match_map_plugin, (void *)&desc)))
		return NULL;
	
	return item->name;
}

reiserfs_plugin_t *reiserfs_plugin_load_by_cords(reiserfs_plugin_type_t type, 
	reiserfs_plugin_id_t id) 
{
	char *name;
	reiserfs_plugin_t *plugin;
		
	/* Looking up code for plugin in plugin map must be here */
	if (!(plugin = reiserfs_plugin_find_in_cashe(type, id))) {
		plugin->h.nlink++;
		return plugin;
	}
	
	/* Loading plugin */
	if (!(name = reiserfs_plugin_find_in_map(type, id))) {
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
	
	plugin->h.nlink--;

	if (!plugin->h.nlink) {
		dlclose(plugin->h.handle);
		aal_list_remove(plugin_cashe, (void *)plugin);
	}	
}

