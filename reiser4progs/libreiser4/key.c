/*
    key.c -- reiserfs key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <reiser4/reiser4.h>

#define DIFF_EL(k1, k2, off)		    \
    ({					    \
	uint64_t e1;			    \
	uint64_t e2;			    \
					    \
	e1 = get_key_el(k1, off);	    \
	e2 = get_key_el(k2, off);	    \
					    \
	e1 < e2 ? -1 : e1 == e2 ? 0 : 1;    \
    })

static const reiserfs_key_t MINIMAL_KEY = {
    .el = { 0ull, 0ull, 0ull }
};

static const reiserfs_key_t MAXIMAL_KEY = {
    .el = { ~0ull, ~0ull, ~0ull }
};

void reiserfs_key_init(reiserfs_key_t *key) {
    aal_assert("vpf-079", key != NULL, return);
    aal_memset(key, 0, sizeof *key);
}

const reiserfs_key_t *reiserfs_min_key(void) {
    return &MINIMAL_KEY;
}

const reiserfs_key_t *reiserfs_max_key(void) {
    return &MAXIMAL_KEY;
}

int reiserfs_key_cmp (reiserfs_key_t *key1, reiserfs_key_t *key2) {
    int result;

    aal_assert("vpf-082", key1 != NULL, return -2);
    aal_assert("vpf-083", key2 != NULL, return -2);
    
    if ((result = DIFF_EL(key1, key2, 0)) == 0)
	if ((result = DIFF_EL(key1, key2, 1)) == 0)
	    result = DIFF_EL(key1, key2, 2);

    return result;
}

static uint64_t pack_string(const char *name, int start_idx) {
    unsigned i;
    uint64_t str;

    str = 0;
    for (i = 0 ; (i < sizeof str - start_idx) && name[i] ; ++ i) {
        str <<= 8;
        str |= (unsigned char) name[i];
    }
    str <<= (sizeof str - i - start_idx) << 3;
    return str;
}

/* Build the entry key */
void build_key_by_entryid(reiserfs_key_t *key, reiserfs_entryid_t *entryid)
{
    aal_assert("vpf-090", key != NULL, return);
    aal_assert("vpf-091", entryid != NULL, return);

    aal_memcpy (&key->el[1], entryid, sizeof *entryid);
    set_key_type(key, KEY_FILE_NAME_MINOR);
}

static void build_entryid_by_key(reiserfs_entryid_t *entryid, reiserfs_key_t *key)
{
    aal_assert("vpf-092", key != NULL, return);
    aal_assert("vpf-093", entryid != NULL, return);

    aal_memcpy (entryid, &key->el[1], sizeof *entryid);

    return;
}

void build_entryid_by_entry_info(reiserfs_entryid_t *entryid, 
    reiserfs_entry_info_t *info/*, reiserfs_dir_t *dir*/) 
{
    uint16_t len; 
    reiserfs_key_t key;
    
    aal_assert("vpf-101", entryid != NULL, return);
    aal_assert("vpf-102", info != NULL, return);

    reiserfs_key_init(&key);
    set_key_type(&key, KEY_FILE_NAME_MINOR);
    len = aal_strlen(info->name);
    if ((len != 1) || aal_strncmp(info->name, ".", 1)) {
	/* Not dot, pack the first part of the name into objectid. */
	set_key_objectid(&key, pack_string(info->name, 1));
	if (len <= OID_CHARS + sizeof(uint64_t)) {
	    /* Fits into objectid + hash. */
	    if (len > OID_CHARS)
		/* Does not fit into objectid, pack the second part of 
		   the name into offset. */
		set_key_offset(&key, pack_string(info->name, 0));			
	} else {
	    /* Note in the key that it is hash, not a name. */
	    key.el[1] |= 0x0100000000000000ull;
	    /* This should be uncomment when directory object will be ready.
	    offset = dir->plugin->dir.hash(info->name + OID_CHARS, len);
	    */
	}
    }

    build_entryid_by_key(entryid, &key);
}



