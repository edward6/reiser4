/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of object plugin functions */

#if !defined( __FS_REISER4_PLUGIN_OBJECT_H__ )
#define __FS_REISER4_PLUGIN_OBJECT_H__

#include "../forward.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/types.h>

extern int lookup_sd(struct inode *inode, znode_lock_mode lock_mode,
		     coord_t * coord, lock_handle * lh, reiser4_key * key);
extern int lookup_sd_by_key(reiser4_tree * tree, znode_lock_mode lock_mode,
			    coord_t * coord, lock_handle * lh, const reiser4_key * key);
extern int guess_plugin_by_mode(struct inode *inode);
extern int common_file_delete(struct inode *inode);
extern int common_file_save(struct inode *inode);
extern int common_build_flow(struct inode *, char *buf, int user,
			     size_t size, loff_t off, rw_op op, flow_t *);
extern int common_write_inode(struct inode *inode);
extern int common_file_owns_item(const struct inode *inode,
				 const coord_t * coord);

extern int common_file_owns_item(const struct inode *inode, const coord_t * coord);
extern reiser4_block_nr common_estimate_update(const struct inode *inode);

/* __FS_REISER4_PLUGIN_OBJECT_H__ */
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
