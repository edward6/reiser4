/*
    key.c -- reiserfs key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#include <reiser4/reiser4.h>

errno_t reiserfs_key_init(reiserfs_key_t *key, const void *data, 
    reiserfs_plugin_t *plugin) 
{
    aal_assert("umka-691", key != NULL, return -1);
    aal_assert("umka-692", plugin != NULL, return -1);
    aal_assert("umka-769", data != NULL, return -1);
    
    aal_memset(key, 0, sizeof(key));
    
    key->plugin = plugin;
    
    aal_memcpy(key->body, data, libreiser4_plugin_call(return -1,
	plugin->key, size,));

    return 0;
}

int reiserfs_key_compare(reiserfs_key_t *key1, reiserfs_key_t *key2) {
    aal_assert("umka-764", key1 != NULL, return -1);
    aal_assert("umka-765", key2 != NULL, return -1);

    return libreiser4_plugin_call(return -1, key1->plugin->key, compare, 
	key1->body, key2->body);
}

void reiserfs_key_clean(reiserfs_key_t *key) {
    aal_assert("umka-675", key != NULL, return);
    aal_assert("umka-676", key->plugin != NULL, return);
    
    libreiser4_plugin_call(return, key->plugin->key, clean, key->body);
} 

errno_t reiserfs_key_build_generic_full(reiserfs_key_t *key, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset) 
{
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return -1, key->plugin->key, 
	build_generic_full, key->body, type, locality, objectid, offset);
}

errno_t reiserfs_key_build_entry_full(reiserfs_key_t *key, 
    reiserfs_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name)
{
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, key->plugin->key, 
	build_entry_full, key->body, hash_plugin, locality, objectid, name);
}

errno_t reiserfs_key_set_type(reiserfs_key_t *key, uint32_t type) {
    aal_assert("umka-686", key != NULL, return -1);
    aal_assert("umka-687", key->plugin != NULL, return -1);

    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_type, key->body, type);
    
    return 0;
}

errno_t reiserfs_key_set_offset(reiserfs_key_t *key, uint64_t offset) {
    aal_assert("umka-688", key != NULL, return -1);
    aal_assert("umka-689", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_offset, key->body, offset);
    
    return 0;
}

errno_t reiserfs_key_set_hash(reiserfs_key_t *key, uint64_t hash) {
    aal_assert("umka-706", key != NULL, return -1);
    aal_assert("umka-707", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_hash, key->body, hash);
    
    return 0;
}

errno_t reiserfs_key_set_objectid(reiserfs_key_t *key, oid_t objectid) {
    aal_assert("umka-694", key != NULL, return -1);
    aal_assert("umka-695", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_objectid, key->body, objectid);

    return 0;
}

errno_t reiserfs_key_set_locality(reiserfs_key_t *key, oid_t locality) {
    aal_assert("umka-696", key != NULL, return -1);
    aal_assert("umka-697", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key, 
	set_locality, key->body, locality);

    return 0;
}

uint32_t reiserfs_key_get_type(reiserfs_key_t *key) {
    aal_assert("umka-698", key != NULL, return -1);
    aal_assert("umka-699", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_type, key->body);
}

uint64_t reiserfs_key_get_offset(reiserfs_key_t *key) {
    aal_assert("umka-700", key != NULL, return -1);
    aal_assert("umka-701", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_offset, key->body);
}

uint64_t reiserfs_key_get_hash(reiserfs_key_t *key) {
    aal_assert("umka-708", key != NULL, return -1);
    aal_assert("umka-709", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_hash, key->body);
}

oid_t reiserfs_key_get_objectid(reiserfs_key_t *key) {
    aal_assert("umka-702", key != NULL, return -1);
    aal_assert("umka-703", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_objectid, key->body);
}

oid_t reiserfs_key_get_locality(reiserfs_key_t *key) {
    aal_assert("umka-704", key != NULL, return -1);
    aal_assert("umka-705", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key, 
	get_locality, key->body);
}

