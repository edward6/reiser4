/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* reiser4 compression mode plugin (used by cryptcompress object plugin) */

#include "../../inode.h"
#include "../plugin.h"
#include "../cryptcompress.h"

/* plugin->should_deflate() */
static int
should_deflate_lazy(cloff_t index)
{
	return !test_bit(0, &index);
}

/* plugin->discard_deflate() */
static int
discard_deflate_smart (struct inode * inode, cloff_t index)
{
	int result = 0;
#if REISER4_DEBUG
	compression_plugin * cplug;
	cplug = inode_compression_plugin(inode);
#endif
	assert("edward-1308", inode != NULL);

	if (index == 0)
		result = force_plugin(inode,
				      PSET_COMPRESSION,
				      compression_plugin_to_plugin
				      (dual_compression_plugin
				       (inode_compression_plugin(inode))));
	if (result)
		return result;
	mark_inode_dirty(inode);

	assert("edward-1331", ergo(index == 0, inode_compression_plugin(inode) ==
				  compression_plugin_by_id(cplug->dual)));
	return 0;
}

/* compression mode_plugins */
compression_mode_plugin compression_mode_plugins[LAST_COMPRESSION_MODE_ID] = {
	[SMART_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = SMART_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "ifcompressible",
			.desc = "If first cluster compressible heuristic",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.should_deflate = NULL,
		.save_deflate = NULL,
		.discard_deflate = discard_deflate_smart
	},
	[FORCE_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = FORCE_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "force",
			.desc = "Compress everything",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.should_deflate = NULL,
		.save_deflate = NULL,
		.discard_deflate = NULL
	},
	[LAZY_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = LAZY_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "test", /* This mode is only for benchmarks */
			.desc = "Don't compress odd clusters",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.should_deflate = should_deflate_lazy,
		.save_deflate = NULL,
		.discard_deflate = NULL
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
