/* Copyright 2001, 2002, 2003 by Hans Reiser,
   licensing governed by reiser4/README */
/* Reiser4 cipher transform plugins */

#include "debug.h"
#include "plugin/plugin.h"
#include "plugin/file/cryptcompress.h"
#include <linux/types.h>
#include <linux/random.h>

#define MAX_CRYPTO_BLOCKSIZE 128

/*
  Default align() method of the crypto-plugin (look for description of this
  method in plugin/plugin.h)

  1) creates the aligning armored format of the input flow before encryption.
     "armored" means that padding is filled by private data (for example,
     pseudo-random sequence of bytes is not private data).
  2) returns length of appended padding

   [ flow | aligning_padding ]
            ^
            |
	  @pad
*/
static int align_stream_common(__u8 * pad,
			       int flow_size /* size of non-aligned flow */,
			       int blocksize /* crypto-block size */)
{
	int pad_size;

	assert("edward-01", pad != NULL);
	assert("edward-02", flow_size != 0);
	assert("edward-03", blocksize != 0
	       || blocksize <= MAX_CRYPTO_BLOCKSIZE);

	pad_size = blocksize - (flow_size % blocksize);
	get_random_bytes(pad, pad_size);
	return pad_size;
}

/* This is used for all the cipher algorithms which do not inflate
   block-aligned data */
static loff_t scale_common(struct inode *inode, size_t blocksize,
			   loff_t src_off /* offset to scale */ )
{
	return src_off;
}

static void free_common (struct crypto_tfm * tfm)
{
	crypto_free_tfm(tfm);
}

static struct crypto_tfm * alloc_aes (void)
{
	return crypto_alloc_tfm ("aes", 0);
}

crypto_plugin crypto_plugins[LAST_CRYPTO_ID] = {
	[NONE_CRYPTO_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = NONE_CRYPTO_ID,
			.pops = NULL,
			.label = "none",
			.desc = "no cipher transform",
			.linkage = {NULL, NULL}
		},
		.alloc = NULL,
		.free = NULL,
		.scale = NULL,
		.align_stream = NULL,
		.setkey = NULL,
		.encrypt = NULL,
		.decrypt = NULL
	},
	[AES_CRYPTO_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = AES_CRYPTO_ID,
			.pops = NULL,
			.label = "aes",
			.desc = "aes cipher transform",
			.linkage = {NULL, NULL}
		},
		.alloc = alloc_aes,
		.free = free_common,
		.scale = scale_common,
		.align_stream = align_stream_common,
		.setkey = NULL,
		.encrypt = NULL,
		.decrypt = NULL
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
