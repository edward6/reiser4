/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__REISER4_PLUGIN_SPACE_BITMAP_H__)
#define __REISER4_PLUGIN_SPACE_BITMAP_H__

#include "../../reiser4.h"

/* declarations of functions implementing methods of space allocator plugin for
 * bitmap based allocator. The functions theirself are in bitmap.c */
int bitmap_init_allocator    (reiser4_space_allocator *, struct super_block *,
			       void *);
int bitmap_destroy_allocator (reiser4_space_allocator *, struct super_block *);
int bitmap_alloc_blocks      (reiser4_space_allocator *,
			       reiser4_blocknr_hint *, int needed,
			       reiser4_block_nr *start, reiser4_block_nr *len);

void bitmap_pre_commit_hook (void);
void bitmap_post_commit_hook (void);
void bitmap_post_write_back_hook (void);

#endif /* __REISER4_PLUGIN_SPACE_BITMAP_H__ */
