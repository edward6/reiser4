/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* reiser4 compression transform plugins */

/* reiser4 compression transform plugins */

#include "debug.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"
#include <linux/types.h>

REGISTER_NONE_ALG(compress, COMPRESS)
     
     
/* GZIP default compression */
     
int alloc_gzip_compress(struct inode * inode)
{
#ifdef CONFIG_CRYPTO
	cryptcompress_info_t * info;
        assert("edward-664", inode != NULL);
	
	info = cryptcompress_inode_data(inode);
	assert("edward-665", info->tfm[COMPRESS_TFM] == NULL);
	
	info->tfm[COMPRESS_TFM] = crypto_alloc_tfm("deflate", 0);
	if (info->tfm[COMPRESS_TFM] == NULL) {
		warning("edward-666", "failed to load transform for deflate\n");
		return -ENOMEM;
	}
#endif
  	return 0;
}     

void free_gzip_compress(struct inode * inode)
{
#ifdef CONFIG_CRYPTO	
	cryptcompress_info_t * info;
        assert("edward-667", inode != NULL);
	
	info = cryptcompress_inode_data(inode);
	assert("edward-668", info->tfm[COMPRESS_TFM] != NULL);
	
	crypto_free_tfm(info->tfm[COMPRESS_TFM]);
	info->tfm[COMPRESS_TFM] = NULL;
#endif	
  	return;
}

compression_plugin compression_plugins[LAST_COMPRESSION_ID] = {
	[NONE_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = NONE_COMPRESSION_ID,
			.pops = NULL,
			.label = "none",
			.desc = "absence of any compression transform",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.alloc = alloc_none_compress,
		.free = free_none_compress,
		.overrun = 0,
	        .compress = NULL,
	        .decompress = NULL
	},
	[GZIP_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = GZIP_COMPRESSION_ID,
			.pops = NULL,
			.label = "gzip",
			.desc = "gzip default level compression",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.alloc = alloc_gzip_compress,
		.free = free_gzip_compress,
		.overrun = 0,
	        .compress = NULL,
	        .decompress = NULL
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

