/* This code encrypts crypto items before flushing them to disk (as
   opposed to encrypting them after each write, which is more
   performance expensive).  

Unresolved issues:

  how do we flag an item as being a crypto item?  Or do we make crypto items distinct item types?  


*/

#include "debug.h"
#include "plugin/plugin.h"
#include <linux/types.h>
#include <linux/random.h>
#define MAX_CRYPTO_BLOCKSIZE 128
#define NONE_EXPKEY_WORDS 8
#define NONE_BLOCKSIZE 8

/* default align() method of the crypto-plugin
   creates the following aligning armored format of the cluster:

   [ cluster | tail ]
               ^         
               |
            @tail
   returns length of tail
*/
static int align_cluster_common(__u8 *tail, int clust_size, int blocksize)
{
	int tail_size;
	
	assert("edward-01", tail != NULL);
	assert("edward-02", clust_size != 0);
	assert("edward-03", blocksize != 0 || blocksize <= MAX_CRYPTO_BLOCKSIZE);
	
	tail_size = blocksize - (clust_size % blocksize);
	get_random_bytes (tail, tail_size);
	return tail_size;
}

/* use this only for symmetric algorithms */
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

/* crypto plugins */
crypto_plugin crypto_plugins[LAST_CRYPTO_ID] = {
	[NONE_CRYPTO_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = NONE_CRYPTO_ID,
			.pops = NULL,
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
