/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* reiser4 digest transform plugin (is used by cryptcompress object plugin) */
/* EDWARD-FIXME-HANS: and it does what? a digest is a what? */
#include "../debug.h"
#include "plugin_header.h"
#include "plugin.h"
#include "file/cryptcompress.h"

#include <linux/types.h>

extern digest_plugin digest_plugins[LAST_DIGEST_ID];

static struct crypto_tfm * alloc_sha256 (void)
{
	return crypto_alloc_tfm ("sha256", 0);
}

static void free_common (struct crypto_tfm * tfm)
{
	crypto_free_tfm(tfm);
}

/* digest plugins */
digest_plugin digest_plugins[LAST_DIGEST_ID] = {
	[SHA256_32_DIGEST_ID] = {
		.h = {
			.type_id = REISER4_DIGEST_PLUGIN_TYPE,
			.id = SHA256_32_DIGEST_ID,
			.pops = NULL,
			.label = "sha256_32",
			.desc = "sha256_32 digest transform",
			.linkage = {NULL, NULL}
		},
		.fipsize = sizeof(__u32),
		.alloc = alloc_sha256,
		.free = free_common
	}
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 120
  scroll-step: 1
  End:
*/
