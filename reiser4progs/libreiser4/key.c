/*
    key.c -- reiser4 common key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/  

#include <reiser4/reiser4.h>

/* Initializes passed key by specified data */
errno_t reiser4_key_init(
    reiser4_key_t *key,	    /* key to be initialized */
    reiser4_plugin_t *plugin,	    /* key plugin to be used */
    const void *data		    /* key data */
) {
    aal_assert("umka-769", data != NULL, return -1);
    aal_assert("umka-691", key != NULL, return -1);
    aal_assert("umka-905", plugin != NULL, return -1);
    
    key->plugin = plugin;
    aal_memset(key->body, 0, sizeof(key->body));
    
    aal_memcpy(key->body, data, plugin_call(return -1,
	key->plugin->key_ops, size,));

    return 0;
}

reiser4_plugin_t *reiser4_key_guess(const void *data) {
    aal_assert("umka-907", data != NULL, return NULL);

    /*
	FIXME-UMKA: Here will be more smart guess code. It should be supporting
	all reiser3 key formats and reiser4 key format.
    */
    return libreiser4_factory_ifind(KEY_PLUGIN_TYPE, KEY_REISER40_ID);
}

/* 
    Compares two keys in plugin independent maner by means of using one of 
    passed keys plugin.
*/
int reiser4_key_compare(
    reiser4_key_t *key1,	    /* the first key for comparing */
    reiser4_key_t *key2	    /* the second one */
) {
    aal_assert("umka-764", key1 != NULL, return -1);
    aal_assert("umka-765", key2 != NULL, return -1);
    aal_assert("umka-906", key1->plugin != NULL, return -1);
    aal_assert("umka-906", key2->plugin != NULL, return -1);
    aal_assert("umka-906", key1->plugin->h.id == key2->plugin->h.id, return -1);

    return plugin_call(return -1, key1->plugin->key_ops, 
	compare, key1->body, key2->body);
}

/* Cleans specified key */
void reiser4_key_clean(
    reiser4_key_t *key		    /* key to be clean */
) {
    aal_assert("umka-675", key != NULL, return);
    aal_assert("umka-676", key->plugin != NULL, return);
    
    plugin_call(return, key->plugin->key_ops, 
	clean, key->body);
} 

/* Builds full non-directory key */
errno_t reiser4_key_build_generic(
    reiser4_key_t *key,	    /* key to be built */
    uint32_t type,		    /* key type to be used */
    oid_t locality,		    /* locality to be used */
    oid_t objectid,		    /* objectid to be used */
    uint64_t offset		    /* offset to be used */
) {
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return plugin_call(return -1, key->plugin->key_ops, 
	build_generic, key->body, type, locality, objectid, offset);
}

/* Builds short non-directory key */
errno_t reiser4_key_build_objid(
    reiser4_key_t *key,	    /* key to be built */
    uint32_t type,		    /* key type */
    oid_t locality,		    /* key locality */
    oid_t objectid		    /* key objectid */
) {
    aal_assert("umka-665", key != NULL, return -1);
    aal_assert("umka-666", key->plugin != NULL, return -1);

    return plugin_call(return -1, key->plugin->key_ops, 
	build_objid, key->body, type, locality, objectid);
}

/* Builds full directory key */
errno_t reiser4_key_build_direntry(
    reiser4_key_t *key,	    /* key to be built */
    reiser4_plugin_t *hash_plugin, /* hash plugin to be used */
    oid_t locality,		    /* loaclity to be used */
    oid_t objectid,		    /* objectid to be used */
    const char *name		    /* entry name to be hashed */
) {
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return plugin_call(return -1, key->plugin->key_ops, 
	build_direntry, key->body, hash_plugin, locality, objectid, name);
}

/* Builds short entry key */
errno_t reiser4_key_build_entryid(
    reiser4_key_t *key,	    /* key to be built */
    reiser4_plugin_t *hash_plugin, /* hash plugin to be used */
    const char *name		    /* entry name */
) {
    aal_assert("umka-668", key != NULL, return -1);
    aal_assert("umka-669", key->plugin != NULL, return -1);
    aal_assert("umka-670", name != NULL, return -1);
    
    return plugin_call(return -1, key->plugin->key_ops, 
	build_entryid, key->body, hash_plugin, name);
}

/* Builds full key by entry short key */
errno_t reiser4_key_build_by_entry(
    reiser4_key_t *key,	    /* key to be built */
    void *data			    /* short entry key data pointer */
) {
    aal_assert("umka-1003", key != NULL, return -1);
    aal_assert("umka-1004", data != NULL, return -1);
    
    return plugin_call(return -1, key->plugin->key_ops, 
	build_by_entry, key->body, data);
}

/* Sets key type */
errno_t reiser4_key_set_type(
    reiser4_key_t *key,	    /* key type will be updated in */
    uint32_t type		    /* new key type */
) {
    aal_assert("umka-686", key != NULL, return -1);
    aal_assert("umka-687", key->plugin != NULL, return -1);

    plugin_call(return -1, key->plugin->key_ops, 
	set_type, key->body, type);
    
    return 0;
}

/* Sets key offset */
errno_t reiser4_key_set_offset(
    reiser4_key_t *key,	    /* key to be updated */
    uint64_t offset		    /* new offset */
) {
    aal_assert("umka-688", key != NULL, return -1);
    aal_assert("umka-689", key->plugin != NULL, return -1);
    
    plugin_call(return -1, key->plugin->key_ops, 
	set_offset, key->body, offset);
    
    return 0;
}

/* Sets key hash component */
errno_t reiser4_key_set_hash(
    reiser4_key_t *key,	    /* key hash will be updated in */
    uint64_t hash		    /* new hash value */
) {
    aal_assert("umka-706", key != NULL, return -1);
    aal_assert("umka-707", key->plugin != NULL, return -1);
    
    plugin_call(return -1, key->plugin->key_ops, 
	set_hash, key->body, hash);
    
    return 0;
}

/* Updates key objectid */
errno_t reiser4_key_set_objectid(
    reiser4_key_t *key,	    /* key objectid will be updated in */
    oid_t objectid		    /* new objectid */
) {
    aal_assert("umka-694", key != NULL, return -1);
    aal_assert("umka-695", key->plugin != NULL, return -1);
    
    plugin_call(return -1, key->plugin->key_ops, 
	set_objectid, key->body, objectid);

    return 0;
}

/* Updates key locality */
errno_t reiser4_key_set_locality(
    reiser4_key_t *key,	    /* key locality will be updated in */
    oid_t locality		    /* new locality */
) {
    aal_assert("umka-696", key != NULL, return -1);
    aal_assert("umka-697", key->plugin != NULL, return -1);
    
    plugin_call(return -1, key->plugin->key_ops, 
	set_locality, key->body, locality);

    return 0;
}

/* Gets key type */
uint32_t reiser4_key_get_type(reiser4_key_t *key) {
    aal_assert("umka-698", key != NULL, return -1);
    aal_assert("umka-699", key->plugin != NULL, return -1);

    return plugin_call(return 0, key->plugin->key_ops, 
	get_type, key->body);
}

/* Returns key offset */
uint64_t reiser4_key_get_offset(reiser4_key_t *key) {
    aal_assert("umka-700", key != NULL, return -1);
    aal_assert("umka-701", key->plugin != NULL, return -1);

    return plugin_call(return 0, key->plugin->key_ops, 
	get_offset, key->body);
}

/* Returns key hash */
uint64_t reiser4_key_get_hash(reiser4_key_t *key) {
    aal_assert("umka-708", key != NULL, return -1);
    aal_assert("umka-709", key->plugin != NULL, return -1);

    return plugin_call(return 0, key->plugin->key_ops, 
	get_hash, key->body);
}

/* Returns key objectid */
oid_t reiser4_key_get_objectid(reiser4_key_t *key) {
    aal_assert("umka-702", key != NULL, return -1);
    aal_assert("umka-703", key->plugin != NULL, return -1);

    return plugin_call(return 0, key->plugin->key_ops, 
	get_objectid, key->body);
}

/* Returns key locality */
oid_t reiser4_key_get_locality(reiser4_key_t *key) {
    aal_assert("umka-704", key != NULL, return -1);
    aal_assert("umka-705", key->plugin != NULL, return -1);

    return plugin_call(return 0, key->plugin->key_ops, 
	get_locality, key->body);
}

/* Returns the maximal possible key  */
void reiser4_key_maximal(reiser4_key_t *key) {
    void *body;
    
    aal_assert("vpf-185", key != NULL, return);
    aal_assert("vpf-186", key->plugin != NULL, return);

    body = plugin_call(return, key->plugin->key_ops, maximal,);

    aal_memcpy(key->body, body, REISER4_KEY_SIZE);
}

/* Returns the minimal possible key */
void reiser4_key_minimal(reiser4_key_t *key) {
    void *body;
    
    aal_assert("vpf-187", key != NULL, return);
    aal_assert("vpf-188", key->plugin != NULL, return);

    body = plugin_call(return, key->plugin->key_ops, minimal,);

    aal_memcpy(key->body, body, REISER4_KEY_SIZE);
}

void reiser4_key_print(reiser4_key_t *key, char *buffer, uint32_t size, 
    uint16_t options) 
{
    aal_assert("vpf-189", key != NULL, return);
    aal_assert("vpf-190", key->plugin != NULL, return);

    plugin_call(return, key->plugin->key_ops, 
	print, key->body, buffer, size, options); 
}
