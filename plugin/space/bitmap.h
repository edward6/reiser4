/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__REISER4_PLUGIN_SPACE_BITMAP_H__)
#define __REISER4_PLUGIN_SPACE_BITMAP_H__

#include "../../reiser4.h"

/* declarations of functions implementing methods of space allocator plugin for
 * bitmap based allocator. The functions theirself are in bitmap.c */
extern int bitmap_init_allocator    (reiser4_space_allocator *, struct super_block *,
			       void *);
extern int bitmap_destroy_allocator (reiser4_space_allocator *, struct super_block *);
extern int bitmap_alloc_blocks      (reiser4_space_allocator *,
			       reiser4_blocknr_hint *, int needed,
			       reiser4_block_nr *start, reiser4_block_nr *len);

extern void bitmap_pre_commit_hook (void);
extern void bitmap_post_commit_hook (void);

typedef __u64 bmap_nr_t;
typedef __u32 bmap_off_t;

/* exported for user-level simulator */
extern void get_bitmap_blocknr (struct super_block *, bmap_nr_t, reiser4_block_nr *);

#endif /* __REISER4_PLUGIN_SPACE_BITMAP_H__ */
