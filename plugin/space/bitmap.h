/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */

#if !defined (__REISER4_PLUGIN_SPACE_BITMAP_H__)
#define __REISER4_PLUGIN_SPACE_BITMAP_H__

#include "../../reiser4.h"

/* Block allocation/deallocation are done through special bitmap objects which
 * are allocated in an array at fs mount. */
struct reiser4_bnode {
	spinlock_t guard;
	char     * wpage; /* working bitmap block */
	char     * cpage; /* commit bitmap block */
};

static inline void spin_lock_bnode (struct reiser4_bnode * bnode)
{
	spin_lock (& bnode -> guard);
}

static inline void spin_unlock_bnode (struct reiser4_bnode * bnode)
{
	spin_unlock (& bnode -> guard);
}

struct bitmap_allocator_data {
	/** an array for bitmap blocks direct access */
	struct reiser4_bnode * bitmap;
};

#define get_barray(super) \
(((struct bitmap_allocator_data *)(get_super_private(super)->space_allocator.u.generic)) -> bitmap)

#define get_bnode(super, i) (get_barray(super) + i)

/* declarations of functions implementing methods of space allocator plugin for
 * bitmap based allocator. The functions theirself are in bitmap.c */
int  bitmap_init_allocator    (reiser4_space_allocator *, struct super_block *,
			       void *);
int  bitmap_destroy_allocator (reiser4_space_allocator *, struct super_block *);
int  bitmap_alloc_blocks      (reiser4_space_allocator *,
			       reiser4_blocknr_hint *, int needed,
			       reiser4_block_nr *start, reiser4_block_nr *len);
void bitmap_dealloc_blocks    (reiser4_space_allocator *,
			       reiser4_block_nr start, reiser4_block_nr len);

#endif /* __REISER4_PLUGIN_SPACE_BITMAP_H__ */
