/*
 * Copyright 2002 Hans Reiser, licensing governed by reiser4/README
 */

#include "reiser4.h"

/*
 * reiser 4.0 default disk layout
 */

/* plugin->u.layout.get_ready */
int layout_40_get_ready (struct super_block * s,
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
