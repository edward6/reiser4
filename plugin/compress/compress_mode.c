/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* This file contains Reiser4 compression mode plugins.

   Compression mode plugin is a set of handlers called by compressor
   at flush time and represent some heuristics including the ones
   which are to avoid compression of incompressible data, see
   http://www.namesys.com/cryptcompress_design.html for more details.
*/
#include "../../inode.h"
#include "../plugin.h"

static int should_deflate_test(struct inode * inode, cloff_t index)
{
	return !test_bit(0, &index);
}

static int should_deflate_none(struct inode * inode, cloff_t index)
{
	return 0;
}

static int should_deflate_common(struct inode * inode, cloff_t index)
{
	return compression_is_on(cryptcompress_inode_data(inode));
}

static int turn_off_compression(struct inode *inode, cloff_t index)
{
	toggle_compression(cryptcompress_inode_data(inode), 0);
	return 0;
}

static int turn_on_compression(struct inode *inode, cloff_t index)
{
	toggle_compression(cryptcompress_inode_data(inode), 1);
	return 0;
}

/* Check on lattice (COL) of some sparseness factor,
   the family of adaptive compression modes which define
   the following behavior:

   Compression is on: try to compress everything and turn
   it off, whenever cluster is incompressible.

   Compression is off: try to compress clusters of indexes
   k * FACTOR (k = 0, 1, 2, ...) and turn it on, if some of
   them is compressible. */

/* check if @index belongs to one-dimensional lattice
   of sparce factor @factor */
static int check_on_lattice(cloff_t index, int factor)
{
	return (factor ? index % factor == 0: index == 0);
}

#define DEFINE_CHECK_ON_LATTICE(FACTOR)                                 \
	static int check_on_lattice_ ## FACTOR (struct inode * inode,   \
						cloff_t index)		\
{                                                                       \
	return should_deflate_common(inode, index) ||			\
		check_on_lattice(index, FACTOR);			\
}

#define SUPPORT_COL_COMPRESSION_MODE(FACTOR, LABEL)                     \
[COL_ ## FACTOR ## _COMPRESSION_MODE_ID] = {                            \
	.h = {                                                          \
		.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,        \
		.id = COL_ ## FACTOR ## _COMPRESSION_MODE_ID,           \
		.pops = NULL,                                           \
		.label = LABEL,                                         \
		.desc = LABEL,                                          \
		.linkage = {NULL, NULL}                                 \
	},                                                              \
	.should_deflate = check_on_lattice_ ## FACTOR,                  \
	.accept_hook =  turn_on_compression,                            \
	.discard_hook = turn_off_compression                            \
}

DEFINE_CHECK_ON_LATTICE(8)
DEFINE_CHECK_ON_LATTICE(16)
DEFINE_CHECK_ON_LATTICE(32)

/* compression mode_plugins */
compression_mode_plugin compression_mode_plugins[LAST_COMPRESSION_MODE_ID] = {
	[NONE_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = NONE_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "none",
			.desc = "Compress nothing",
			.linkage = {NULL, NULL}
		},
		.should_deflate = should_deflate_none,
		.accept_hook = NULL,
		.discard_hook = NULL
	},
	/* Check-on-lattice adaptive compression modes.
	   Turn compression on/off in flush time */
	SUPPORT_COL_COMPRESSION_MODE(8, "col8"),
	SUPPORT_COL_COMPRESSION_MODE(16, "col16"),
	SUPPORT_COL_COMPRESSION_MODE(32, "col32"),
	/* This compression mode enables file conversion, i.e. ->write() checks
	   whether the first complete
	   logical cluster (of index #0) is compressible. If not, then items are
	   converted to extents, and management is passed to unix file plugin */
	[CONVX_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = CONVX_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "convx",
			.desc = "Convert to extent",
			.linkage = {NULL, NULL}
		},
		.should_deflate = should_deflate_common,
		.accept_hook = NULL,
		.discard_hook = NULL
	},
	[FORCE_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = FORCE_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "force",
			.desc = "Force to compress everything",
			.linkage = {NULL, NULL}
		},
		.should_deflate = NULL,
		.accept_hook = NULL,
		.discard_hook = NULL
	},
	[TEST_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = TEST_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "test", /* This mode is for benchmarks only */
			.desc = "Don't compress odd clusters",
			.linkage = {NULL, NULL}
		},
		.should_deflate = should_deflate_test,
		.accept_hook = NULL,
		.discard_hook = NULL
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
