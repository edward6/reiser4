/*
    key.c -- reiserfs key code.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Vitaly Fertman.
*/  

#include <reiserfs/reiserfs.h>

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

static uint64_t reiserfs_pack_string(const char *name, int start_idx) {
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


