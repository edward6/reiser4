/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */
/* Internal item contains down-link to the child of the internal/twig
   node in a tree. It is internal items that are actually used during
   tree traversal. */

#if !defined( __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__ )
#define __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__

#include "../../forward.h"
#include "../../dformat.h"

#if 0
#endif

/* on-disk layout of internal item */
typedef struct internal_item_layout {
	/*  0 */ reiser4_dblock_nr pointer;
	/*  4 */
} internal_item_layout;

int internal_mergeable(const coord_t * p1, const coord_t * p2);
lookup_result internal_lookup(const reiser4_key * key, lookup_bias bias, coord_t * coord);
/* store pointer from internal item into "block". Implementation of
    ->down_link() method */
extern void internal_down_link(const coord_t * coord, const reiser4_key * key, reiser4_block_nr * block);
extern int internal_has_pointer_to(const coord_t * coord, const reiser4_block_nr * block);
extern int internal_create_hook(const coord_t * item, void *arg);
extern int internal_kill_hook(const coord_t * item, unsigned from, unsigned count, void *);
extern int internal_shift_hook(const coord_t * item, unsigned from, unsigned count, znode * old_node);
extern void internal_print(const char *prefix, coord_t * coord);

extern int internal_utmost_child(const coord_t * coord, sideof side, jnode ** child);
int internal_utmost_child_real_block(const coord_t * coord, sideof side, reiser4_block_nr * block);

extern void internal_update(const coord_t * coord, 
			    const reiser4_block_nr * blocknr);
extern int internal_check(const coord_t * coord, const char **error);

/* __FS_REISER4_PLUGIN_ITEM_INTERNAL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
