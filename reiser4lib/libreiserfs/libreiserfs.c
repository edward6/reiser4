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

aal_list_t *plugin_cashe = NULL;
aal_list_t *plugin_map = NULL;

int libreiserfs_get_max_interface_version(void) {
	return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiserfs_get_min_interface_version(void) {
	return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiserfs_get_version(void) {
	return VERSION;
}

static void _init(void) __attribute__ ((constructor));

static void _init(void) {
#ifdef ENABLE_NLS
	 bindtextdomain(PACKAGE, LOCALEDIR);
#endif
	 
	plugin_cashe = aal_list_create(10);
	plugin_map = aal_list_create(10);
}

static void _done(void) __attribute__ ((destructor));

static void _done(void) {
	aal_list_free(plugin_cashe);
	aal_list_free(plugin_map);
}

