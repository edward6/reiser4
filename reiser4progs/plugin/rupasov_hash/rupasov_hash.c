/*
    rupasov_hash.c -- rupasov hash.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/plugin.h>

uint64_t rupasov_hash_build(const unsigned char *name, uint32_t len) {
    uint32_t j, pow;
    uint32_t i;
    uint64_t a, c;
	
    for (pow = 1, i = 1; i < len; i++) pow = pow * 10; 
	
    if (len == 1) 
	a = name[0] - 48;
    else
	a = (name[0] - 48) * pow;
	
    for (i = 1; i < len; i++) {
	c = name[i] - 48; 
	
	for (pow = 1, j = i; j < len - 1; j++) 
	    pow = pow * 10;
	
	a = a + c * pow;
    }
	
    for (; i < 40; i++) {
	c = '0' - 48;
	
	for (pow = 1,j = i; j < len - 1; j++) 
	    pow = pow * 10;
	
	a = a + c * pow;
    }
	
    for (; i < 256; i++) {
	c = i; 
	
	for (pow = 1, j = i; j < len - 1; j++) 
	    pow = pow * 10;
	
	a = a + c * pow;
    }
	
    a = a << 7;
    return a;
}

static reiserfs_plugin_t rupasov_hash_plugin = {
    .hash_ops = {
	.h = {
	    .handle = NULL,
	    .id = HASH_RUPASOV_ID,
	    .type = HASH_PLUGIN_TYPE,
	    .label = "rupasov_hash",
	    .desc = "Implementation of rupasov hash for reiserfs 4.0, ver. " VERSION,
	},
	.build = (uint64_t (*)(const unsigned char *, uint32_t))rupasov_hash_build
    }
};

static reiserfs_plugin_t *rupasov_hash_entry(reiserfs_core_t *c) {
    return &rupasov_hash_plugin;
}

libreiser4_factory_register(rupasov_hash_entry);

