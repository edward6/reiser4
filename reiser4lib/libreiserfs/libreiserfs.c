/*
 	libreiserfs.c -- version control functions and library initialization code.
	Copyright (C) 1996-2002 Hans Reiser
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiserfs/reiserfs.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

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

static int libreiserfs_init_plugins(void) {
	return 0;
}

static void libreiserfs_release_plugins(void) {
	int i;
	for (i = aal_list_count(plugins) - 1; i >= 0; i--)
		reiserfs_plugin_unload((reiserfs_plugin_t *)aal_list_at(plugins, i));
}

static void _init(void) __attribute__ ((constructor));

static void _init(void) {
#ifdef ENABLE_NLS
	 bindtextdomain(PACKAGE, LOCALEDIR);
#endif
	plugins = aal_list_create(10);
	libreiserfs_init_plugins();
}

static void _done(void) __attribute__ ((destructor));

static void _done(void) {
	libreiserfs_release_plugins();
	aal_list_free(plugins);
}

