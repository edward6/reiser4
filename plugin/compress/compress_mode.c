/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* Reiser4 compression mode plugins. 
   See Handling incompressible data,
   http://www.namesys.com/cryptcompress_design.html
*/
#include "../../inode.h"
#include "../plugin.h"

static int should_deflate_test(cloff_t index)
{
	return !test_bit(0, &index);
}

static int should_deflate_none(cloff_t index)
{
	return 0;
}

/* generic turn on/off compression */
int switch_compression(struct inode *inode)
{
	int result;

	result = force_plugin(inode,
			      PSET_COMPRESSION,
			      compression_plugin_to_plugin
			      (dual_compression_plugin
			       (inode_compression_plugin(inode))));
	if (result)
		return result;
	__mark_inode_dirty(inode, I_DIRTY_PAGES);
	return 0;
}

static int switch_compression_on_zero(struct inode *inode, cloff_t index)
{
	assert("edward-1308", inode != NULL);
	return (index ? 0 : switch_compression(inode));
}

static int turn_off_compression(struct inode *inode, cloff_t index)
{
	return (inode_compression_plugin(inode)->compress ?
		switch_compression(inode) : 0);
}

static int turn_on_compression(struct inode *inode, cloff_t index)
{
	return (inode_compression_plugin(inode)->compress ?
		0 : switch_compression(inode));
}

/* Check on lattice (COL) of some sparseness factor,
   the family of compression modes.

   Turn compression off whenever compressor detects
   incompressible data.
   Try to compress clusters of indexes k * FACTOR (k = 0, 1, 2, ...)
   and turn compression on, if some of them is compressible */

/* check if @index belongs to one-dimensional lattice
   of sparce factor @factor */
static int check_on_lattice(cloff_t index, int factor)
{
	return (factor ? index % factor == 0: index == 0);
}

#define DEFINE_CHECK_ON_LATTICE(FACTOR)                          \
static int check_on_lattice_ ## FACTOR (cloff_t index)           \
{                                                                \
	return check_on_lattice(index, FACTOR);                  \
}                               

#define SUPPORT_COL_COMPRESSION_MODE(FACTOR, LABEL)              \
[COL_ ## FACTOR ## _COMPRESSION_MODE_ID] = {                     \
	.h = {                                                   \
		.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE, \
		.id = COL_ ## FACTOR ## _COMPRESSION_MODE_ID,    \
		.pops = NULL,                                    \
		.label = LABEL,                                  \
		.desc = LABEL,                                   \
		.linkage = {NULL, NULL}                          \
	},                                                       \
	.should_deflate = check_on_lattice_ ## FACTOR,           \
	.accept_hook =  turn_on_compression,                     \
	.discard_hook = turn_off_compression                     \
}

DEFINE_CHECK_ON_LATTICE(8)
DEFINE_CHECK_ON_LATTICE(16)
DEFINE_CHECK_ON_LATTICE(32)

/* compression mode_plugins */
compression_mode_plugin compression_mode_plugins[LAST_COMPRESSION_MODE_ID] = {
	/* Check-on-lattice compression modes */
	SUPPORT_COL_COMPRESSION_MODE(8, "col8"),
	SUPPORT_COL_COMPRESSION_MODE(16, "col16"),
	SUPPORT_COL_COMPRESSION_MODE(32, "col32"),
	/* Turn off compression forever if logical cluster 
	   of index == 0 is incompressible */
	[COZ_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = COZ_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "coz",
			.desc = "Check on zero",
			.linkage = {NULL, NULL}
		},
		.should_deflate = NULL,
		.accept_hook = NULL,
		.discard_hook = switch_compression_on_zero
	},
	[FORCE_COMPRESSION_MODE_ID] = {
		.h = {
			.type_id = REISER4_COMPRESSION_MODE_PLUGIN_TYPE,
			.id = FORCE_COMPRESSION_MODE_ID,
			.pops = NULL,
			.label = "force",
			.desc = "Compress everything",
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
	},
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
