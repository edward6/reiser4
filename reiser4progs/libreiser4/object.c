/*
    object.c -- common code for files and directories.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

static error_t reiserfs_object_find_stat(reiserfs_key_t *key, const char *name) {
    return -1;
}

reiserfs_object_t *reiserfs_object_open(reiserfs_fs_t *fs, const char *name) {
    reiserfs_object_t *object;
    
    aal_assert("umka-678", fs != NULL, return NULL);
    aal_assert("umka-679", name != NULL, return NULL);

    if (!(object = aal_calloc(sizeof(*object), 0)))
	return NULL;

    return object;
}

void reiserfs_object_close(reiserfs_object_t *object) {
    aal_assert("umka-680", object != NULL, return);
    aal_free(object);
}

