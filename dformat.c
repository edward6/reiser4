/* Copyright 2002 Hans Reiser */

#include "reiser4.h"
#define REISER4_FIRST_BITMAP_BLOCK 100

/** A number of bitmap blocks for given fs. This number can be stored on disk
 * or calculated on fly; it depends on disk format. */
int get_nr_bmap (struct super_block * super)
{
	reiser4_super_info_data * info_data = get_super_private(super); 

	assert ("zam-391", super->s_blocksize > 0);
	assert ("zam-392", info_data != NULL);
	assert ("zam-393", info_data->blocks_used != 0);

	return (info_data->blocks_used - 1) / super->s_blocksize + 1;
}


/** return a physical disk address for logical bitmap number @bmap */
void get_bitmap_blocknr (struct super_block * super, int bmap, reiser4_block_nr *bnr)
{

	assert ("zam-389", bmap >= 0);
	assert ("zam-390", bmap < get_nr_bmap(super));

	/* FIXME_ZAM: before discussing of disk layouts and disk format
	 * plugins I implement bitmap location scheme which is close to scheme
	 * used in reiser 3.6 */
	if (bmap == 0) {
		*bnr = REISER4_FIRST_BITMAP_BLOCK;
	} else {
		*bnr = bmap * super->s_blocksize * 8;
	}
}


/* FIXME-VS: standard reiser4 disk layout plugin goes below. This will move to
 * plugin/layout/layout40.c or so */

layout_40_super_info_data * get_layout_40_super_private (struct super_block * s)
{
	assert ("vs-477", get_super_private (s));
	assert ("vs-478", get_super_private (s)->layout_private);
	return get_super_private (s)->layout_private;
}


/* plugin->u.layout.get_ready */
static int layout_40_get_ready (struct super_block * s,
				reiser4_super_info_data * super_info,
				struct buffer_head * super_bh UNUSED_ARG)
{
	assert ("vs-475", s != NULL);
	assert ("vs-474", get_super_private (s) == super_info);
	return 0;
}

/* plugin->u.layout.root_dir_key */
const reiser4_key * layout_40_root_dir_key (void)
{
	static const reiser4_key LAYOUT_40_ROOT_DIR_KEY = {
		.el = { { ( 2 << 4 ) | KEY_SD_MINOR }, { 42ull }, { 0ull } }
	};

	return &LAYOUT_40_ROOT_DIR_KEY;
}

/* plugin->u.layout.oids_free */
long layout_40_oids_free (struct super_block * s)
{
	__u64 result;
	long long_max;

	result = oids_free (&get_layout_40_super_private (s)->oid_allocator);
	long_max = ~(long)0 >> 1;
	return result > (__u64)long_max ? (long)-1 : (long)result;
}

/* plugin->u.layout.oids_used */
long layout_40_oids_used (struct super_block * s)
{
	__u64 result;
	long long_max;

	result = oids_used (&get_layout_40_super_private (s)->oid_allocator);
	long_max = ~(long)0 >> 1;
	return result > (__u64)long_max ? (long)-1 : (long)result;	
}

/* plugin->u.layout.allocate_oid */
int layout_40_allocate_oid (struct super_block * s, oid_t * result)
{
	return allocate_oid (&get_layout_40_super_private (s)->oid_allocator,
			     result);
}

/* plugin->u.layout.release_oid */
int layout_40_release_oid (struct super_block * s, oid_t result)
{
	return release_oid (&get_layout_40_super_private (s)->oid_allocator,
			    result);
}


reiser4_plugin layout_plugins[ LAST_LAYOUT_ID ] = {
	[ LAYOUT_40_ID ] = {
		.h = {
			.type_id = REISER4_LAYOUT_PLUGIN_TYPE,
			.id      = LAYOUT_40_ID,
			.pops    = NULL,
			.label   = "reiser40",
			.desc    = "standard disk layout for reiser40",
			.linkage = TS_LIST_LINK_ZERO,
		},
		.u = {
			.layout = {
				.get_ready    = layout_40_get_ready,
				.root_dir_key = layout_40_root_dir_key,
				.oids_used    = layout_40_oids_used,
				.oids_free    = layout_40_oids_free,
				.allocate_oid = layout_40_allocate_oid,
				.release_oid  = layout_40_release_oid
			}
		}
	}
};



/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
