/*
    libreiser4.c -- version control functions and library initialization code.
    Copyright (C) 1996-2002 Hans Reiser
    Author Yury Umanets.
*/

#include <reiser4/reiser4.h>

int libreiser4_get_max_interface_version(void) {
    return LIBREISERFS_MAX_INTERFACE_VERSION;
}

int libreiser4_get_min_interface_version(void) {
    return LIBREISERFS_MIN_INTERFACE_VERSION;
}

const char *libreiser4_get_version(void) {
    return VERSION;
}

errno_t libreiser4_init(void) {
    if (libreiser4_factory_init()) {
	aal_exception_throw(EXCEPTION_FATAL, EXCEPTION_OK, 
	    "Can't initialize plugin factory.");
	return -1;
    }
    return 0;
}

void libreiser4_done(void) {
    libreiser4_factory_done();
}

