/*
    dir.c -- directory specific code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <reiser4/reiser4.h>

reiserfs_object_t *reiserfs_dir_open(reiserfs_fs_t *fs, 
    const char *name) 
{
    reiserfs_object_t *dir;

    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;

    return reiserfs_object_open(fs, name);
}

void reiserfs_dir_close(reiserfs_object_t *object) {
    reiserfs_object_close(object);
}

#ifndef ENABLE_COMPACT

reiserfs_object_t *reiserfs_dir_create(reiserfs_fs_t *fs,
    reiserfs_object_hint_t *hint, reiserfs_plugin_t *plugin,
    reiserfs_object_t *parent, const char *name) 
{
    reiserfs_object_t *dir;
    
    if (!(dir = aal_calloc(sizeof(*dir), 0)))
	return NULL;

    return reiserfs_object_create(fs, hint, plugin, parent, name);
}

errno_t reiserfs_dir_add(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint) 
{
    aal_assert("umka-862", object != NULL, return -1);
    aal_assert("umka-863", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
        add, object->entity, hint);
}

#endif

errno_t reiserfs_dir_rewind(reiserfs_object_t *object) {
    aal_assert("umka-842", object != NULL, return -1);
    aal_assert("umka-843", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	rewind, object->entity);
}

errno_t reiserfs_dir_read(reiserfs_object_t *object, 
    reiserfs_entry_hint_t *hint) 
{
    aal_assert("umka-860", object != NULL, return -1);
    aal_assert("umka-861", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
        read, object->entity, hint);
}

uint32_t reiserfs_dir_tell(reiserfs_object_t *object) {
    aal_assert("umka-875", object != NULL, return -1);
    aal_assert("umka-876", object->entity != NULL, return -1);

    return libreiser4_plugin_call(return -1, object->plugin->dir_ops, 
	tell, object->entity);
}

