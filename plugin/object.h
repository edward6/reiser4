/* Copyright 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Declaration of object plugin functions. */

#if !defined( __FS_REISER4_PLUGIN_OBJECT_H__ )
#define __FS_REISER4_PLUGIN_OBJECT_H__

#include "../type_safe_hash.h"

/* common implementations of inode operations */
int create_common(struct inode *parent, struct dentry *dentry,
		  int mode, struct nameidata *);
struct dentry *lookup_common(struct inode *parent, struct dentry *dentry,
			     struct nameidata *nameidata);
int link_common(struct dentry *existing, struct inode *parent,
		struct dentry *newname);
int unlink_common(struct inode *parent, struct dentry *victim);
int mkdir_common(struct inode *parent, struct dentry *dentry, int mode);
int symlink_common(struct inode *parent, struct dentry *dentry,
		   const char *linkname);
int mknod_common(struct inode *parent, struct dentry *dentry,
		 int mode, dev_t rdev);
int rename_common(struct inode *old_dir, struct dentry *old_name,
		  struct inode *new_dir, struct dentry *new_name);
void *follow_link_common(struct dentry *, struct nameidata *data);
int permission_common(struct inode *, int mask,	/* mode bits to check permissions for */
		      struct nameidata *nameidata);
int setattr_common(struct dentry *, struct iattr *);
int getattr_common(struct vfsmount *mnt, struct dentry *, struct kstat *);

/* common implementations of file operations */
loff_t llseek_common_dir(struct file *, loff_t off, int origin);
int readdir_common(struct file *, void *dirent, filldir_t);
int release_dir_common(struct inode *, struct file *);
int sync_common(struct file *, struct dentry *, int datasync);

/* common implementations of address space operations */
int prepare_write_common(struct file *, struct page *, unsigned from,
			 unsigned to);

/* file plugin operations: common implementations */
int write_sd_by_inode_common(struct inode *);
int key_by_inode_and_offset_common(struct inode *, loff_t, reiser4_key *);
int set_plug_in_inode_common(struct inode *object, struct inode *parent,
			     reiser4_object_create_data *);
int adjust_to_parent_common(struct inode *object, struct inode *parent,
			    struct inode *root);
int adjust_to_parent_common_dir(struct inode *object, struct inode *parent,
				struct inode *root);
int adjust_to_parent_cryptcompress(struct inode *object, struct inode *parent,
				   struct inode *root);
int create_object_common(struct inode *object, struct inode *parent,
			 reiser4_object_create_data *);
int delete_object_common(struct inode *);
int delete_directory_common(struct inode *);
int add_link_common(struct inode *object, struct inode *parent);
int rem_link_common(struct inode *object, struct inode *parent);
int rem_link_common_dir(struct inode *object, struct inode *parent);
int owns_item_common(const struct inode *, const coord_t *);
int owns_item_common_dir(const struct inode *, const coord_t *);
int can_add_link_common(const struct inode *);
int can_rem_link_common_dir(const struct inode *);
int detach_common_dir(struct inode *child, struct inode *parent);
int open_cryptcompress(struct inode * inode, struct file * file);
int bind_common_dir(struct inode *child, struct inode *parent);
int safelink_common(struct inode *, reiser4_safe_link_t, __u64 value);
reiser4_block_nr estimate_create_common(const struct inode *);
reiser4_block_nr estimate_create_common_dir(const struct inode *);
reiser4_block_nr estimate_update_common(const struct inode *);
reiser4_block_nr estimate_unlink_common(const struct inode *,
					const struct inode *);
reiser4_block_nr estimate_unlink_common_dir(const struct inode *,
					    const struct inode *);
char *wire_write_common(struct inode *, char *start);
char *wire_read_common(char *addr, reiser4_object_on_wire *);
struct dentry *wire_get_common(struct super_block *, reiser4_object_on_wire *);
int wire_size_common(struct inode *);
void wire_done_common(reiser4_object_on_wire *);

/* dir plugin operations: common implementations */
struct dentry *get_parent_common(struct inode *child);
int is_name_acceptable_common(const struct inode *, const char *name, int len);
void build_entry_key_common(const struct inode *,
			    const struct qstr *qname, reiser4_key *);
int build_readdir_key_common(struct file *dir, reiser4_key *);
int add_entry_common(struct inode *object, struct dentry *where,
		     reiser4_object_create_data *, reiser4_dir_entry_desc *);
int rem_entry_common(struct inode *object, struct dentry *where,
		     reiser4_dir_entry_desc *);
int init_common(struct inode *object, struct inode *parent,
		reiser4_object_create_data *);
int done_common(struct inode *);
int attach_common(struct inode *child, struct inode *parent);
int detach_common(struct inode *object, struct inode *parent);
reiser4_block_nr estimate_add_entry_common(const struct inode *);
reiser4_block_nr estimate_rem_entry_common(const struct inode *);
reiser4_block_nr dir_estimate_unlink_common(const struct inode *,
					    const struct inode *);

/* these are essential parts of common implementations, they are to make
   customized implementations easier */
int do_prepare_write(struct file *, struct page *, unsigned from, unsigned to);

/* merely useful functions */
int lookup_sd(struct inode *, znode_lock_mode, coord_t *, lock_handle *,
	      const reiser4_key *, int silent);

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
