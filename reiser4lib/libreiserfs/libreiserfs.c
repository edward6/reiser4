/*
 	libreiserfs.c -- version control functions and library initialization code.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef ENABLE_ALONE
#  include <sys/types.h>
#  include <dirent.h>
#endif

#include <reiserfs/reiserfs.h>

aal_list_t *plugins = NULL;

int libreiserfs_get_max_interface_version(void) {
	return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiserfs_get_min_interface_version(void) {
	return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiserfs_get_version(void) {
	return VERSION;
}

int libreiserfs_init(void) {
#ifndef ENABLE_ALONE
	DIR *dir;
	struct dirent *ent;
#endif	

	plugins = aal_list_create(10);
	
#ifndef ENABLE_ALONE
	if (!(dir = opendir(PLUGIN_DIR))) {
		aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, "umka-023", 
			"Can't open directory %s.", PLUGIN_DIR);
		return 0;
	}
	
	while ((ent = readdir(dir))) {
		char plug_name[4096];
		reiserfs_plugin_t *plugin;

		if ((strlen(ent->d_name) == 1 && aal_strncmp(ent->d_name, ".", 1)) ||
				(strlen(ent->d_name) == 2 && aal_strncmp(ent->d_name, "..", 2)))
			continue;	
	
		if (strlen(ent->d_name) <= 2)
			continue;
		
		if (ent->d_name[strlen(ent->d_name) - 2] != 's' || 
				ent->d_name[strlen(ent->d_name) - 1] != 'o')
			continue;
		
		aal_memset(plug_name, 0, sizeof(plug_name));
		aal_snprintf(plug_name, sizeof(plug_name), "%s/%s", PLUGIN_DIR, ent->d_name);
		if (!(plugin = reiserfs_plugin_load(plug_name, "reiserfs_plugin_info"))) {
			aal_exception_throw(EXCEPTION_WARNING, EXCEPTION_IGNORE, "umka-024", 
				"Can't load plugin %s.", plug_name);
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
	return aal_list_count(plugins) > 0;
}

void libreiserfs_done(void) {
	while (aal_list_count(plugins) > 0) {
		reiserfs_plugin_unload((reiserfs_plugin_t *)aal_list_at(plugins, 
			aal_list_count(plugins) - 1));
	}
	
	aal_list_free(plugins);
}

