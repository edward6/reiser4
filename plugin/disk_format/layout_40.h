/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

int layout_40_get_ready (struct super_block * s,
			 reiser4_super_info_data * super_info,
			 struct buffer_head * super_bh);
const reiser4_key * layout_40_root_dir_key (void);
