/*
    key.c -- reiserfs key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#include <reiser4/reiser4.h>

void reiserfs_key_clean(reiserfs_key_t *key, reiserfs_plugin_t *key_plugin) {
    aal_assert("umka-675", key != NULL, return);
    aal_assert("umka-676", key_plugin != NULL, return);
    
    libreiser4_plugin_call(return, key_plugin->key, clean, &key);
} 

error_t reiserfs_key_build_file_key(reiserfs_key_t *key, 
    reiserfs_plugin_t *key_plugin, uint32_t type, oid_t locality, 
    oid_t objectid, uint64_t offset) 
{
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key_plugin != NULL, return -1);

    return libreiser4_plugin_call(return -1, key_plugin->key, build_file_key, 
	key, type, locality, objectid, offset);
}

error_t reiserfs_key_build_dir_key(reiserfs_key_t *key, 
    reiserfs_plugin_t *key_plugin, reiserfs_plugin_t *hash_plugin, 
    oid_t locality, oid_t objectid, const char *name)
{
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key_plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, key_plugin->key, build_dir_key, 
	key, hash_plugin, locality, objectid, name);
}

