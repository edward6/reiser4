/*
    key.c -- reiserfs common key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#include <reiser4/reiser4.h>

/* Initializes passed key by specified data */
errno_t reiserfs_key_init(
    reiserfs_key_t *key,	    /* key to be initialized */
    reiserfs_plugin_t *plugin,	    /* key plugin to be used */
    const void *data		    /* key data */
) {
    aal_assert("umka-769", data != NULL, return -1);
    aal_assert("umka-691", key != NULL, return -1);
    aal_assert("umka-905", plugin != NULL, return -1);
    
    key->plugin = plugin;
    aal_memset(key->body, 0, sizeof(key->body));
    
    aal_memcpy(key->body, data, libreiser4_plugin_call(return -1,
	key->plugin->key_ops, size,));

    return 0;
}

reiserfs_plugin_t *reiserfs_key_guess(const void *data) {
    aal_assert("umka-907", data != NULL, return NULL);

    /*
	FIXME-UMKA: Here will be more smart guess code. It should be supporting
	all reiser3 key formats and reiser4 key format.
    */
    return libreiser4_factory_find(REISERFS_KEY_PLUGIN, 0x0);
}

/* 
    Compares two keys in plugin independent maner by means of using one of 
    passed keys plugin.
*/
int reiserfs_key_compare_full(
    reiserfs_key_t *key1,	    /* the first key for comparing */
    reiserfs_key_t *key2	    /* the second one */
) {
    aal_assert("umka-764", key1 != NULL, return -1);
    aal_assert("umka-765", key2 != NULL, return -1);
    aal_assert("umka-906", key1->plugin != NULL, return -1);
    aal_assert("umka-906", key2->plugin != NULL, return -1);
    aal_assert("umka-906", key1->plugin->h.id == key2->plugin->h.id, return -1);

    return libreiser4_plugin_call(return -1, key1->plugin->key_ops, 
	compare_full, key1->body, key2->body);
}

/* Compares two companents of passed keys (locality and objectid) */
int reiserfs_key_compare_short(
    reiserfs_key_t *key1,	    /* the first key for comparing */
    reiserfs_key_t *key2	    /* the second one */
) {
    aal_assert("umka-764", key1 != NULL, return -1);
    aal_assert("umka-765", key2 != NULL, return -1);
    aal_assert("umka-906", key1->plugin != NULL, return -1);
    aal_assert("umka-906", key2->plugin != NULL, return -1);
    aal_assert("umka-906", key1->plugin->h.id == key2->plugin->h.id, return -1);

    return libreiser4_plugin_call(return -1, key1->plugin->key_ops, 
	compare_short, key1->body, key2->body);
}

/* Cleans specified key */
void reiserfs_key_clean(
    reiserfs_key_t *key		    /* key to be clean */
) {
    aal_assert("umka-675", key != NULL, return);
    aal_assert("umka-676", key->plugin != NULL, return);
    
    libreiser4_plugin_call(return, key->plugin->key_ops, 
	clean, key->body);
} 

/* Builds full non-directory key */
errno_t reiserfs_key_build_generic_full(
    reiserfs_key_t *key,	    /* key to be built */
    uint32_t type,		    /* key type to be used */
    oid_t locality,		    /* locality to be used */
    oid_t objectid,		    /* objectid to be used */
    uint64_t offset		    /* offset to be used */
) {
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	build_generic_full, key->body, type, locality, objectid, offset);
}

/* Builds short non-directory key */
errno_t reiserfs_key_build_generic_short(
    reiserfs_key_t *key,	    /* key to be built */
    uint32_t type,		    /* key type */
    oid_t locality,		    /* key locality */
    oid_t objectid		    /* key objectid */
) {
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	build_generic_short, key->body, type, locality, objectid);
}

/* Builds full directory key */
errno_t reiserfs_key_build_entry_full(
    reiserfs_key_t *key,	    /* key to be built */
    reiserfs_plugin_t *hash_plugin, /* hash plugin to be used */
    oid_t locality,		    /* loaclity to be used */
    oid_t objectid,		    /* objectid to be used */
    const char *name		    /* entry name to be hashed */
) {
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	build_entry_full, key->body, hash_plugin, locality, objectid, name);
}

/* Builds short entry key */
errno_t reiserfs_key_build_entry_short(
    reiserfs_key_t *key,	    /* key to be built */
    reiserfs_plugin_t *hash_plugin, /* hash plugin to be used */
    const char *name		    /* entry name */
) {
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	build_entry_short, key->body, hash_plugin, name);
}

/* Sets key type */
errno_t reiserfs_key_set_type(
    reiserfs_key_t *key,	    /* key type will be updated in */
    uint32_t type		    /* new key type */
) {
    aal_assert("umka-686", key != NULL, return -1);
    aal_assert("umka-687", key->plugin != NULL, return -1);

    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_type, key->body, type);
    
    return 0;
}

/* Sets key offset */
errno_t reiserfs_key_set_offset(
    reiserfs_key_t *key,	    /* key to be updated */
    uint64_t offset		    /* new offset */
) {
    aal_assert("umka-688", key != NULL, return -1);
    aal_assert("umka-689", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_offset, key->body, offset);
    
    return 0;
}

/* Sets key hash component */
errno_t reiserfs_key_set_hash(
    reiserfs_key_t *key,	    /* key hash will be updated in */
    uint64_t hash		    /* new hash value */
) {
    aal_assert("umka-706", key != NULL, return -1);
    aal_assert("umka-707", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_hash, key->body, hash);
    
    return 0;
}

/* Updates key objectid */
errno_t reiserfs_key_set_objectid(
    reiserfs_key_t *key,	    /* key objectid will be updated in */
    oid_t objectid		    /* new objectid */
) {
    aal_assert("umka-694", key != NULL, return -1);
    aal_assert("umka-695", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_objectid, key->body, objectid);

    return 0;
}

/* Updates key locality */
errno_t reiserfs_key_set_locality(
    reiserfs_key_t *key,	    /* key locality will be updated in */
    oid_t locality		    /* new locality */
) {
    aal_assert("umka-696", key != NULL, return -1);
    aal_assert("umka-697", key->plugin != NULL, return -1);
    
    libreiser4_plugin_call(return -1, key->plugin->key_ops, 
	set_locality, key->body, locality);

    return 0;
}

/* Gets key type */
uint32_t reiserfs_key_get_type(reiserfs_key_t *key) {
    aal_assert("umka-698", key != NULL, return -1);
    aal_assert("umka-699", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key_ops, 
	get_type, key->body);
}

/* Returns key offset */
uint64_t reiserfs_key_get_offset(reiserfs_key_t *key) {
    aal_assert("umka-700", key != NULL, return -1);
    aal_assert("umka-701", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key_ops, 
	get_offset, key->body);
}

/* Returns key hash */
uint64_t reiserfs_key_get_hash(reiserfs_key_t *key) {
    aal_assert("umka-708", key != NULL, return -1);
    aal_assert("umka-709", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key_ops, 
	get_hash, key->body);
}

/* Returns key objectid */
oid_t reiserfs_key_get_objectid(reiserfs_key_t *key) {
    aal_assert("umka-702", key != NULL, return -1);
    aal_assert("umka-703", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key_ops, 
	get_objectid, key->body);
}

/* Returns key locality */
oid_t reiserfs_key_get_locality(reiserfs_key_t *key) {
    aal_assert("umka-704", key != NULL, return -1);
    aal_assert("umka-705", key->plugin != NULL, return -1);

    return libreiser4_plugin_call(return 0, key->plugin->key_ops, 
	get_locality, key->body);
}

