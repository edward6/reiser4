/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* crypto-hash functions */

#include "../debug.h"
#include "plugin_header.h"
#include "plugin.h"

#include <linux/types.h>

#define NONE_BLOCK_SIZE 64
#define NONE_DIGEST_SIZE 16

static int none_alloc(void * ctx)
{
	return 0;
}

static void none_free(void *ctx)
{
}

static void none_init(void *ctx)
{
}

static void none_update (void *ctx, const __u8 *data, unsigned int len)
{	
}

static void none_final (void *ctx, __u8 *out)
{
	memset(out, 0, NONE_DIGEST_SIZE); 
}


/* digest plugins */
digest_plugin digest_plugins[LAST_DIGEST_ID] = {
	[NONE_DIGEST_ID] = {
		.h = {
			.type_id = REISER4_DIGEST_PLUGIN_TYPE,
			.id = NONE_DIGEST_ID,
			.pops = NULL,
			.label = "none",
			.desc = "trivial digest",
			.linkage = TS_LIST_LINK_ZERO}
		,
		.blksize = NONE_BLOCK_SIZE,
		.digestsize = NONE_DIGEST_SIZE,
	        .alloc = none_alloc,
	        .free = none_free,
	        .init = none_init,
	        .update = none_update,
	        .final = none_final}
};
