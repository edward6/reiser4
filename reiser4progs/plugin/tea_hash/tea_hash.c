/*
    tea_hash.c -- tea hash.
    Copyright (C) 1996-2002 Hans Reiser.
    Author Yury Umanets.
*/

#include <reiser4/plugin.h>

#define FULLROUNDS  10
#define PARTROUNDS  6
#define DELTA	    0x9E3779B9

#define tea_hash_core(rounds)					\
do {								\
    uint64_t sum = 0;						\
    int n = rounds;						\
    uint64_t b0, b1;						\
								\
    b0 = h0;							\
    b1 = h1;							\
								\
    do {							\
	sum += DELTA;						\
	b0 += ((b1 << 4) + a) ^ (b1 + sum) ^ ((b1 >> 5) + b);	\
	b1 += ((b0 << 4) + c) ^ (b0 + sum) ^ ((b0 >> 5) + d);	\
    } while (--n);						\
								\
    h0 += b0;							\
    h1 += b1;							\
} while(0)

uint64_t tea_hash_build(const unsigned char *name, uint32_t len) {
    uint64_t k[] = { 0x9464a485, 0x542e1a94, 0x3e846bff, 0xb75bcfc3}; 

    uint64_t i;
    uint64_t pad;
    uint64_t a, b, c, d;
    uint64_t h0 = k[0], h1 = k[1];
 
    pad = (uint64_t)len | ((uint64_t)len << 8);
    pad |= pad << 16;

    while(len >= 16) {
	a = (uint64_t)name[0]      |
	    (uint64_t)name[1] << 8 |
	    (uint64_t)name[2] << 16|
	    (uint64_t)name[3] << 24;
	
	b = (uint64_t)name[4]      |
	    (uint64_t)name[5] << 8 |
	    (uint64_t)name[6] << 16|
	    (uint64_t)name[7] << 24;
	
	c = (uint64_t)name[8]       |
	    (uint64_t)name[9] << 8  |
	    (uint64_t)name[10] << 16|
	    (uint64_t)name[11] << 24;
	
	d = (uint64_t)name[12]      |
	    (uint64_t)name[13] << 8 |
	    (uint64_t)name[14] << 16|
	    (uint64_t)name[15] << 24;
	
	tea_hash_core(PARTROUNDS);
	    
	len -= 16;
	name += 16;
    }

    if (len >= 12) {
        if (len >= 16)
            *(int *)0 = 0;

	    a = (uint64_t)name[ 0]      |
	        (uint64_t)name[ 1] << 8 |
	        (uint64_t)name[ 2] << 16|
	        (uint64_t)name[ 3] << 24;
	    
	    b = (uint64_t)name[ 4]      |
	        (uint64_t)name[ 5] << 8 |
	        (uint64_t)name[ 6] << 16|
	        (uint64_t)name[ 7] << 24;
	    
	    c = (uint64_t)name[ 8]      |
	        (uint64_t)name[ 9] << 8 |
		(uint64_t)name[10] << 16|
		(uint64_t)name[11] << 24;

	    d = pad;
	    for(i = 12; i < len; i++) {
	    	d <<= 8;
	    	d |= name[i];
	    }
    } else if (len >= 8) {
        if (len >= 12)
            *(int *)0 = 0;
        a = (uint64_t)name[ 0]      |
	    (uint64_t)name[ 1] << 8 |
	    (uint64_t)name[ 2] << 16|
	    (uint64_t)name[ 3] << 24;
	
	b = (uint64_t)name[ 4]      |
	    (uint64_t)name[ 5] << 8 |
	    (uint64_t)name[ 6] << 16|
	    (uint64_t)name[ 7] << 24;
	
	c = d = pad;
	    
	for (i = 8; i < len; i++) {
	    c <<= 8;
	    c |= name[i];
	}
    } else if (len >= 4) {
        if (len >= 8)
	    *(int *)0 = 0;
	
	a = (uint64_t)name[ 0]      |
	    (uint64_t)name[ 1] << 8 |
	    (uint64_t)name[ 2] << 16|
	    (uint64_t)name[ 3] << 24;
	
	b = c = d = pad;
	for (i = 4; i < len; i++) {
	    b <<= 8;
	    b |= name[i];
	}
    } else {
	if (len >= 4)
	    *(int *)0 = 0;
	
	a = b = c = d = pad;
	for(i = 0; i < len; i++) {
	    a <<= 8;
	    a |= name[i];
	}
    }

    tea_hash_core(FULLROUNDS);

    return h0 ^ h1;
}
static reiser4_plugin_t tea_hash_plugin = {
    .hash_ops = {
	.h = {
	    .handle = NULL,
	    .id = HASH_TEA_ID,
	    .type = HASH_PLUGIN_TYPE,
	    .label = "tea_hash",
	    .desc = "Implementation of tea hash for reiserfs 4.0, ver. " VERSION,
	},
	.build = tea_hash_build
    }
};

static reiser4_plugin_t *tea_hash_start(reiser4_core_t *c) {
    return &tea_hash_plugin;
}

libreiser4_factory_register(tea_hash_start);

