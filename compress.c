/* compression plugins */


#include "debug.h"
#include "plugin/plugin.h"
#include <linux/types.h>

#define NONE_CLUSTER_SIZE 4096

static void none_compress (__u8 *buf, __u8 *src_first, unsigned *src_len,
			    __u8 *dst_first, unsigned *dst_len)
{
	assert("edward-17", buf != NULL);
	assert("edward-18", src_first != NULL);
	assert("edward-19", src_len != NULL);
	assert("edward-20", dst_first != NULL);
	assert("edward-21", dst_len != NULL);
	assert("edward-22", *src_len != 0 && *src_len <= NONE_CLUSTER_SIZE);
	
	*dst_len = *src_len;
	memcpy(dst_first, src_first, *src_len);
}
			   
/* compression plugins */
compression_plugin compression_plugins[LAST_COMPRESSION_ID] = {
		[NONE_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = NONE_CRYPTO_ID,
			.pops = NULL,
			.label = "none",
			.desc = "Null compression",
			.linkage = TS_LIST_LINK_ZERO}
		,
		.mem_req = NONE_CLUSTER_SIZE,
	        .compress = none_compress,
	        .decompress = none_compress}
};
