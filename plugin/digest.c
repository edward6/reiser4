/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* NIKITA-FIXME-HANS: digest plugins do what?  why no comments */

/* crypto-hash functions */

#include "../debug.h"
#include "plugin_header.h"
#include "plugin.h"

#include <linux/types.h>

#define NONE_BLOCK_SIZE 64
#define NONE_DIGEST_SIZE 16

static int alloc_none(void * ctx)
{
	return 0;
}

static void free_none(void *ctx)
{
}

static void init_none(void *ctx)
{
}

static void update_none(void *ctx, const __u8 *data, unsigned int len)
{	
}

static void final_none(void *ctx, __u8 *out)
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
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.blksize = NONE_BLOCK_SIZE,
		.digestsize = NONE_DIGEST_SIZE,
	        .alloc = alloc_none,
	        .free = free_none,
	        .init = init_none,
	        .update = update_none,
	        .final = final_none
	}
};
