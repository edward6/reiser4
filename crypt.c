/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* Crypto-plugins for reiser4 cryptcompress objects */

#include "debug.h"
#include "plugin/plugin.h"
#include <linux/types.h>
#include <linux/random.h>
#define MAX_CRYPTO_BLOCKSIZE 128
#define NONE_EXPKEY_WORDS 8
#define NONE_BLOCKSIZE 8

/* default align() method of the crypto-plugin
   1) creates the following aligning armored format of the input flow before encryption :

   [ flow | aligninig_padding ]
            ^   
            |
	  @pad
  2) returns length of appended padding
*/
static int align_cluster_common(__u8 *pad, int clust_size, int blocksize)
{
	int pad_size;
	
	assert("edward-01", pad != NULL);
	assert("edward-02", clust_size != 0);
	assert("edward-03", blocksize != 0 || blocksize <= MAX_CRYPTO_BLOCKSIZE);
	
	pad_size = blocksize - (clust_size % blocksize);
	get_random_bytes (pad, pad_size);
	return pad_size;
}

/* use this only for symmetric algorithms 
EDWARD-FIXME-HANS: go through all your code and write function headers that explain what the function does.  For instance, what is "scale".*/
static loff_t scale_common(struct inode * inode UNUSED_ARG,
			   size_t blocksize UNUSED_ARG,
			   loff_t src_off)
{
	return src_off;
}

static size_t blocksize_none (__u16 keysize UNUSED_ARG /* keysize bits */)
{
	return NONE_BLOCKSIZE;
}

static int set_key_none(__u32 *expkey, const __u8 *key UNUSED_ARG)
{
	memset(expkey, 0, NONE_EXPKEY_WORDS * sizeof(__u32));
	return 0;	
}

static void crypt_none (__u32 *expkey UNUSED_ARG, __u8 *dst, const __u8 *src)
{
	assert("edward-04", dst != NULL);
	assert("edward-05", src != NULL);

	memcpy(dst, src, NONE_BLOCKSIZE);
}

/* EDWARD-FIXME-HANS: why is this not in the plugin directory? */

/* crypto plugins */
crypto_plugin crypto_plugins[LAST_CRYPTO_ID] = {
	[NONE_CRYPTO_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = NONE_CRYPTO_ID,
			.pops = NULL,
			/* this is a special crypto algorithm which
			   doesn't change data, this is useful for
			   debuging purposes and various benchmarks */
			.label = "none",
			.desc = "Id rearrangement",
			.linkage = TS_LIST_LINK_ZERO
		},
		.nr_keywords = NONE_EXPKEY_WORDS,
		.blocksize = blocksize_none,
		.scale = scale_common,
	        .align_cluster = align_cluster_common,
	        .set_key = set_key_none,
	        .encrypt = crypt_none,
	        .decrypt = crypt_none
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
