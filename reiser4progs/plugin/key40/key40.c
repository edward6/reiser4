/*
    key40.c -- reiser4 default key plugin.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include "key40.h"

static reiser4_core_t *core = NULL;

static const key40_t MINIMAL_KEY = {
    .el = { 0ull, 0ull, 0ull }
};

static const key40_t MAXIMAL_KEY = {
    .el = { ~0ull, ~0ull, ~0ull }
};

static reiser4_body_t *key40_minimal(void) {
    return (key40_t *)&MINIMAL_KEY;
}

static reiser4_body_t *key40_maximal(void) {
    return (key40_t *)&MAXIMAL_KEY;
}

static int key40_compare_short(key40_t *key1, 
    key40_t *key2) 
{
    int result;

    if ((result = k40_comp_el(key1, key2, 0)) != 0)
	return result;

    return k40_comp_el(key1, key2, 1);
}

static int key40_compare(reiser4_body_t *body1, 
    reiser4_body_t *body2) 
{
    int result;
    key40_t *key1, *key2;

    aal_assert("vpf-135", body1 != NULL, return -1);
    aal_assert("vpf-136", body2 != NULL, return -1);
    
    key1 = (key40_t *)body1;
    key2 = (key40_t *)body2;
    
    if ((result = key40_compare_short(key1, key2)) != 0)
	return result;

    return k40_comp_el(key1, key2, 2);
}

static int key40_confirm(reiser4_body_t *body) {
    aal_assert("vpf-137", body != NULL, return -1);
    return 1;
}

static errno_t key40_valid(reiser4_body_t *body) {
    aal_assert("vpf-137", body != NULL, return -1);
    return -1;
}

static void key40_set_type(reiser4_body_t *body, 
    uint32_t type)
{
    aal_assert("umka-634", body != NULL, return);
    k40_set_type((key40_t *)body, (reiser4_key40_minor_t)type);
}

static uint32_t key40_get_type(reiser4_body_t *body) {
    aal_assert("umka-635", body != NULL, return 0);
    return (uint32_t)k40_get_type((key40_t *)body);
}

static void key40_set_locality(reiser4_body_t *body, 
    oid_t locality) 
{
    aal_assert("umka-636", body != NULL, return);
    k40_set_locality((key40_t *)body, (uint64_t)locality);
}

static oid_t key40_get_locality(reiser4_body_t *body) {
    aal_assert("umka-637", body != NULL, return 0);
    return (oid_t)k40_get_locality((key40_t *)body);
}
    
static void key40_set_objectid(reiser4_body_t *body, 
    oid_t objectid) 
{
    aal_assert("umka-638", body != NULL, return);
    k40_set_objectid((key40_t *)body, (uint64_t)objectid);
}

static oid_t key40_get_objectid(reiser4_body_t *body) {
    aal_assert("umka-639", body != NULL, return 0);
    return (oid_t)k40_get_objectid((key40_t *)body);
}

static void key40_set_offset(reiser4_body_t *body, 
    uint64_t offset)
{
    aal_assert("umka-640", body != NULL, return);
    k40_set_offset((key40_t *)body, offset);
}

static uint64_t key40_get_offset(reiser4_body_t *body) {
    aal_assert("umka-641", body != NULL, return 0);
    return k40_get_offset((key40_t *)body);
}

static void key40_set_hash(reiser4_body_t *body, 
    uint64_t hash)
{
    aal_assert("vpf-129", body != NULL, return);
    k40_set_hash((key40_t *)body, hash);
}

static uint64_t key40_get_hash(reiser4_body_t *body) {
    aal_assert("vpf-130", body != NULL, return 0);
    return k40_get_hash((key40_t *)body);
}

static uint8_t key40_size(void) {
    return sizeof(key40_t);
}

static void key40_clean(reiser4_body_t *body) {
    aal_assert("vpf-139", body != NULL, return);
    aal_memset(body, 0, key40_size());
}

static uint64_t key40_pack_string(const char *name, 
    uint32_t start) 
{
    unsigned i;
    uint64_t str;

    aal_assert("vpf-134", name != NULL, return 0);
    
    str = 0;
    for (i = 0; (i < sizeof(str) - start) && name[i]; ++i) {
        str <<= 8;
        str |= (unsigned char)name[i];
    }
    str <<= (sizeof(str) - i - start) << 3;
    
    return str;
}

static errno_t key40_build_hash(key40_t *key,
    reiser4_plugin_t *hash_plugin, const char *name) 
{
    uint16_t len;
    oid_t objectid, offset;
    
    aal_assert("vpf-101", key != NULL, return -1);
    aal_assert("vpf-102", name != NULL, return -1);
    aal_assert("vpf-128", hash_plugin != NULL, return -1); 
    
    len = aal_strlen(name);
    
    if (len == 1 && name[0] == '.')
	return 0;
    
    /* 
        Not dot, pack the first part of the name into 
        objectid.
    */
    objectid = key40_pack_string(name, 1);
    
    if (len <= OID_CHARS + sizeof(uint64_t)) {
	offset = 0ull;

        if (len > OID_CHARS) {
	    /* 
		Does not fit into objectid, pack the second part of 
		the name into offset. 
	    */
	    offset = key40_pack_string(name + OID_CHARS, 0);
	}
    } else {
	objectid = 0x0100000000000000ull;
	offset = hash_plugin->hash_ops.build((const char *)(name + OID_CHARS),
	    aal_strlen(name) - OID_CHARS);
    }
    
    key40_set_objectid(key, objectid);
    key40_set_offset(key, offset);

    return 0;
}

static errno_t key40_build_direntry(reiser4_body_t *body, 
    reiser4_plugin_t *hash_plugin, oid_t locality, 
    oid_t objectid, const char *name) 
{
    key40_t *key = (key40_t *)body;
    
    aal_assert("vpf-140", body != NULL, return -1);
    aal_assert("umka-667", name != NULL, return -1);
    aal_assert("umka-1006", hash_plugin != NULL, return -1);
    
    key40_clean(key);

    k40_set_locality(key, objectid);
    k40_set_type(key, KEY40_FILENAME_MINOR);
    
    key40_build_hash(key, hash_plugin, name);

    return 0;
}

static errno_t key40_build_entryid(reiser4_body_t *body, 
    reiser4_plugin_t *hash_plugin, const char *name) 
{
    key40_t key;    
    
    aal_assert("vpf-142", body != NULL, return -1);
    
    key40_clean(&key);
    key40_build_hash(&key, hash_plugin, name);
    
    aal_memset(body, 0, sizeof(uint64_t)*2);
    aal_memcpy(body, &key.el[1], sizeof(uint64_t)*2);

    return 0;
}

static errno_t key40_build_generic(reiser4_body_t *body, 
    uint32_t type, oid_t locality, oid_t objectid, uint64_t offset) 
{
    key40_t *key = (key40_t *)body;
    
    aal_assert("vpf-141", body != NULL, return -1);

    key40_clean(key);
    
    k40_set_locality(key, locality);
    k40_set_type(key, (reiser4_key40_minor_t)type);
    k40_set_objectid(key, objectid);
    k40_set_offset(key, offset);

    return 0;
}

static errno_t key40_build_objid(reiser4_body_t *body, uint32_t type, 
    oid_t locality, oid_t objectid)
{
    key40_t key;
    
    aal_assert("vpf-143", body != NULL, return -1);
    
    key40_clean(&key);

    k40_set_locality(&key, locality);
    k40_set_type(&key, (reiser4_key40_minor_t)type);
    k40_set_objectid(&key, objectid);
    
    aal_memset(body, 0, sizeof(uint64_t)*2);
    aal_memcpy(body, &key, sizeof(uint64_t)*2);

    return 0;
}

static errno_t key40_build_by_entry(reiser4_body_t *body, 
    void *data)
{
    key40_t *key = (key40_t *)body;
    
    aal_assert("umka-877", body != NULL, return -1);
    aal_assert("umka-878", data != NULL, return -1);
    
    key40_clean(key);
    aal_memcpy(&key->el[1], data, sizeof(uint64_t) * 2);
    
    return 0;
}

extern errno_t key40_print(reiser4_body_t *body, char *buff, 
    uint32_t n, uint16_t options);

static reiser4_plugin_t key40_plugin = {
    .key_ops = {
	.h = {
	    .handle = NULL,
	    .id = KEY_REISER40_ID,
	    .type = KEY_PLUGIN_TYPE,
	    .label = "key40",
	    .desc = "Key for reiserfs 4.0, ver. " VERSION,
	},
	
	.confirm	= key40_confirm,
	.valid		= key40_valid,
	.size		= key40_size,
	.minimal	= key40_minimal,
	.maximal	= key40_maximal,
	.clean		= key40_clean,
	.compare	= key40_compare,
	.print		= key40_print,

	.set_type	= key40_set_type,
	.get_type	= key40_get_type,

	.set_locality	= key40_set_locality,
	.get_locality	= key40_get_locality,

	.set_objectid	= key40_set_objectid,
	.get_objectid	= key40_get_objectid,

	.set_offset	= key40_set_offset,
	.get_offset	= key40_get_offset,
	
	.set_hash	= key40_set_hash,
	.get_hash	= key40_get_hash,
	
	.build_generic  = key40_build_generic,
	.build_direntry = key40_build_direntry,
	
	.build_objid	= key40_build_objid,
	.build_entryid  = key40_build_entryid,
	
	.build_by_entry	= key40_build_by_entry
    }
};

static reiser4_plugin_t *key40_start(reiser4_core_t *c) {
    core = c;
    return &key40_plugin;
}

libreiser4_factory_register(key40_start);

