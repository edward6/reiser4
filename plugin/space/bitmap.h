/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__REISER4_PLUGIN_SPACE_BITMAP_H__)
#define __REISER4_PLUGIN_SPACE_BITMAP_H__

#include "../../dformat.h"
#include "space_allocator.h"
#include "../../block_alloc.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */

/* declarations of functions implementing methods of space allocator plugin for
 * bitmap based allocator. The functions theirself are in bitmap.c */
extern int bitmap_init_allocator(reiser4_space_allocator *, struct super_block *, void *);
extern int bitmap_destroy_allocator(reiser4_space_allocator *, struct super_block *);
extern int bitmap_alloc_blocks(reiser4_space_allocator *,
			       reiser4_blocknr_hint *, int needed, reiser4_block_nr * start, reiser4_block_nr * len);
#if REISER4_DEBUG

extern void bitmap_check_blocks(const reiser4_block_nr *, const reiser4_block_nr *, int);

#endif

extern void bitmap_dealloc_blocks(reiser4_space_allocator *, reiser4_block_nr, reiser4_block_nr);
extern void bitmap_pre_commit_hook(void);

typedef __u64 bmap_nr_t;
typedef __u32 bmap_off_t;

/* exported for user-level simulator */
extern void get_bitmap_blocknr(struct super_block *, bmap_nr_t, reiser4_block_nr *);

#endif				/* __REISER4_PLUGIN_SPACE_BITMAP_H__ */
