/*
    key.c -- reiserfs key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#include <reiser4/reiser4.h>

void reiserfs_key_init(reiserfs_key_t *key, reiserfs_plugin_t *key_plugin) {
    aal_assert("umka-691", key != NULL, return);
    aal_assert("umka-692", key_plugin != NULL, return);
    
    key->plugin = key_plugin;
}

reiserfs_key_t *reiserfs_key_create(reiserfs_plugin_t *key_plugin) {
    reiserfs_key_t *key;
    
    aal_assert("umka-690", key_plugin != NULL, return NULL);

    if (!(key != aal_calloc(sizeof(*key), 0)))
	return NULL;
    
    reiserfs_key_init(key, key_plugin); 
    return key;
}	

void reiserfs_key_done(reiserfs_key_t *key) {
    aal_assert("umka-693", key != NULL, return);
    aal_free(key);
}

void reiserfs_key_clean(reiserfs_key_t *key) {
    aal_assert("umka-675", key != NULL, return);
    aal_assert("umka-676", key->plugin != NULL, return);
    
    libreiser4_plugin_call(return, key->plugin->key, clean, key);
} 

error_t reiserfs_key_build_file_key(reiserfs_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset) 
{
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return -1, key->plugin->key, build_file_key, 
	key, type, locality, objectid, offset);
}

error_t reiserfs_key_build_dir_key(reiserfs_key_t *key, 
    reiserfs_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name)
{
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, key->plugin->key, build_dir_key, 
	key, hash_plugin, locality, objectid, name);
}

error_t reiserfs_key_set_type(reiserfs_key_t *key, uint32_t type) {
    aal_assert("umka-686", key != NULL, return -1);
    aal_assert("umka-687", key->plugin != NULL, return -1);

    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_type, key, type);
    return 0;
}

error_t reiserfs_key_set_offset(reiserfs_key_t *key, uint64_t offset) {
    aal_assert("umka-688", key != NULL, return -1);
    aal_assert("umka-689", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_offset, key, offset);
    
    return 0;
}

error_t reiserfs_key_set_hash(reiserfs_key_t *key, uint64_t hash) {
    aal_assert("umka-706", key != NULL, return -1);
    aal_assert("umka-707", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_hash, key, hash);
    
    return 0;
}

error_t reiserfs_key_set_objectid(reiserfs_key_t *key, oid_t objectid) {
    aal_assert("umka-694", key != NULL, return -1);
    aal_assert("umka-695", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_objectid, key, objectid);

    return 0;
}

error_t reiserfs_key_set_locality(reiserfs_key_t *key, oid_t locality) {
    aal_assert("umka-696", key != NULL, return -1);
    aal_assert("umka-697", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_locality, key, locality);

    return 0;
}

uint32_t reiserfs_key_get_type(reiserfs_key_t *key) {
    aal_assert("umka-698", key != NULL, return -1);
    aal_assert("umka-699", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_type, key);
}

uint64_t reiserfs_key_get_offset(reiserfs_key_t *key) {
    aal_assert("umka-700", key != NULL, return -1);
    aal_assert("umka-701", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_offset, key);
}

uint64_t reiserfs_key_get_hash(reiserfs_key_t *key) {
    aal_assert("umka-708", key != NULL, return -1);
    aal_assert("umka-709", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_hash, key);
}

oid_t reiserfs_key_get_objectid(reiserfs_key_t *key) {
    aal_assert("umka-702", key != NULL, return -1);
    aal_assert("umka-703", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_objectid, key);
}

oid_t reiserfs_key_get_locality(reiserfs_key_t *key) {
    aal_assert("umka-704", key != NULL, return -1);
    aal_assert("umka-705", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_locality, key);
}

