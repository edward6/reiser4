/*
    libreiserfs.c -- version control functions and library initialization code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#include <reiserfs/reiserfs.h>

int libreiserfs_get_max_interface_version(void) {
    return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiserfs_get_min_interface_version(void) {
    return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiserfs_get_version(void) {
    return VERSION;
}

error_t libreiserfs_init(void) {
    return reiserfs_plugins_init();
}

void libreiserfs_fini(void) {
    reiserfs_plugins_fini();
}

