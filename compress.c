/* compression plugins */


#include "debug.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"
#include <linux/types.h>

static void none_compress (__u8 *buf, __u8 *src_first, unsigned src_len,
			    __u8 *dst_first, unsigned *dst_len)
{
	assert("edward-17", buf != NULL);
	assert("edward-18", src_first != NULL);
	assert("edward-19", src_len != 0);
	assert("edward-20", dst_first != NULL);
	assert("edward-21", dst_len != NULL);
	
	*dst_len = src_len;
	memcpy(dst_first, src_first, src_len);
}
			   
/* compression plugins */
compression_plugin compression_plugins[LAST_COMPRESSION_ID] = {
		[NONE_COMPRESSION_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_PLUGIN_TYPE,
			.id = NONE_COMPRESSION_ID,
			.pops = NULL,
			.label = "none",
			.desc = "Null compression",
			.linkage = TS_LIST_LINK_ZERO}
		,
		.mem_req = MIN_CLUSTER_SIZE,
	        .compress = none_compress,
	        .decompress = none_compress}
};
