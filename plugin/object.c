/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Examples of object plugins: file, directory, symlink, special file */
/* Plugins associated with inode:
  
   Plugin of inode is plugin referenced by plugin-id field of on-disk
   stat-data. How we store this plugin in in-core inode is not
   important. Currently pointers are used, another variant is to store
   offsets and do array lookup on each access.
  
   Now, each inode has one selected plugin: object plugin that
   determines what type of file this object is: directory, regular etc.
  
   This main plugin can use other plugins that are thus subordinated to
   it. Directory instance of object plugin uses hash; regular file
   instance uses tail policy plugin.
  
   Object plugin is either taken from id in stat-data or guessed from
   i_mode bits. Once it is established we ask it to install its
   subordinate plugins, by looking again in stat-data or inheriting them
   from parent.
*/
/* How new inode is initialized during ->read_inode():
    1 read stat-data and initialize inode fields: i_size, i_mode, 
      i_generation, capabilities etc.
    2 read plugin id from stat data or try to guess plugin id 
      from inode->i_mode bits if plugin id is missing.
    3 Call ->init_inode() method of stat-data plugin to initialise inode fields.
    4 Call ->activate() method of object's plugin. Plugin is either read from
      from stat-data or guessed from mode bits
    5 Call ->inherit() method of object plugin to inherit as yet initialized
      plugins from parent.
  
   Easy induction proves that on last step all plugins of inode would be
   initialized.
  
   When creating new object:
    1 obtain object plugin id (see next period)
    2 ->install() this plugin
    3 ->inherit() the rest from the parent
  
*/
/* We need some examples of creating an object with default and
  non-default plugin ids.  Nikita, please create them.
 
*/

#include "../forward.h"
#include "../debug.h"
#include "../key.h"
#include "../kassign.h"
#include "../coord.h"
#include "../seal.h"
#include "plugin_header.h"
#include "item/static_stat.h"
#include "file/file.h"
#include "symlink.h"
#include "dir/dir.h"
#include "item/item.h"
#include "oid/oid.h"
#include "plugin.h"
#include "object.h"
#include "../znode.h"
#include "../tap.h"
#include "../tree.h"
#include "../vfs_ops.h"
#include "../inode.h"
#include "../super.h"
#include "../reiser4.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/quotaops.h>

/* helper function to print errors */
static void
key_warning(const reiser4_key * key /* key to print */,
	    int code /* error code to print */)
{
	assert("nikita-716", key != NULL);

	warning("nikita-717", "Error for inode %llu (%i)", 
		get_key_objectid(key), code);
	print_key("for key", key);
}

#if REISER4_DEBUG
static void
check_inode_seal(const struct inode *inode,
		 const coord_t *coord, const reiser4_key *key)
{
	reiser4_key unit_key;

	unit_key_by_coord(coord, &unit_key);
	assert("nikita-2752", 
	       WITH_DATA_RET(coord->node, 1, keyeq(key, &unit_key)));
	assert("nikita-2753", get_inode_oid(inode) == get_key_objectid(key));
}

static void
check_sd_coord(const coord_t *coord, const reiser4_key *key)
{
	reiser4_key ukey;

	if (zload(coord->node))
		return;

	if (!coord_is_existing_unit(coord) ||
	    !item_plugin_by_coord(coord) ||
	    !keyeq(unit_key_by_coord(coord, &ukey), key) ||
	    (znode_get_level(coord->node) != LEAF_LEVEL) || 
	    !item_is_statdata(coord)) {
		warning("nikita-1901", "Conspicuous seal");
		print_key("key", key);
		print_coord("coord", coord, 1);
		impossible("nikita-2877", "no way");
	}
	zrelse(coord->node);
}

#else
#define check_inode_seal(inode, coord, key) noop
#define check_sd_coord(coord, key) noop
#endif



/* find sd of inode in a tree, deal with errors */
int
lookup_sd(struct inode *inode /* inode to look sd for */ ,
	  znode_lock_mode lock_mode /* lock mode */ ,
	  coord_t * coord /* resulting coord */ ,
	  lock_handle * lh /* resulting lock handle */ ,
	  reiser4_key * key /* resulting key */ )
{
	assert("nikita-1692", inode != NULL);
	assert("nikita-1693", coord != NULL);
	assert("nikita-1694", key != NULL);

	build_sd_key(inode, key);
	return lookup_sd_by_key(tree_by_inode(inode), lock_mode, coord, lh, key);
}

/* find sd of inode in a tree, deal with errors */
int
lookup_sd_by_key(reiser4_tree * tree /* tree to look in */ ,
		 znode_lock_mode lock_mode /* lock mode */ ,
		 coord_t * coord /* resulting coord */ ,
		 lock_handle * lh /* resulting lock handle */ ,
		 const reiser4_key * key /* resulting key */ )
{
	int result;
	__u32 flags;

	assert("nikita-718", tree != NULL);
	assert("nikita-719", coord != NULL);
	assert("nikita-720", key != NULL);

	result = 0;
	/* look for the object's stat data in a tree. 
	   This returns in "node" pointer to a locked znode and in "pos"
	   position of an item found in node. Both are only valid if
	   coord_found is returned. */
	flags = (lock_mode == ZNODE_WRITE_LOCK) ? CBK_FOR_INSERT : 0;
	flags |= CBK_UNIQUE;
	result = coord_by_key(tree, key, coord, lh, lock_mode, 
			      FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, flags, 0/*ra_info*/);
	if (REISER4_DEBUG && result == 0)
		check_sd_coord(coord, key);

	if (result != 0)
		key_warning(key, result);
	return result;
}

/* insert new stat-data into tree. Called with inode state
    locked. Return inode state locked. */
static int
insert_new_sd(struct inode *inode /* inode to create sd for */ )
{
	int result;
	reiser4_key key;
	coord_t coord;
	reiser4_item_data data;
	char *area;
	reiser4_inode *ref;
	lock_handle lh;
	oid_t oid;

	assert("nikita-723", inode != NULL);

	/* stat data is already there */
	if (!inode_get_flag(inode, REISER4_NO_SD))
		return 0;

	ref = reiser4_inode_data(inode);
	spin_lock_inode(inode);
	grab_plugin_from(ref, sd, inode_sd_plugin(inode));

	data.iplug = ref->pset->sd;
	data.length = data.iplug->s.sd.save_len(inode);
	spin_unlock_inode(inode);

	data.data = NULL;
	data.user = 0;

	if (data.length > tree_by_inode(inode)->nplug->max_item_size()) {
		/* This is silly check, but we don't know actual node where
		   insertion will go into. */
		return RETERR(-ENAMETOOLONG);
	}
	result = oid_allocate(&oid);

	if (result != 0)
		return result;

	set_inode_oid(inode, oid);

	coord_init_zero(&coord);
	init_lh(&lh);

	result = insert_by_key(tree_by_inode(inode), build_sd_key(inode, &key), &data, &coord, &lh,
			       /* stat data lives on a leaf level */
			       LEAF_LEVEL, inter_syscall_ra(inode), NO_RAP, CBK_UNIQUE);
	/* we don't want to re-check that somebody didn't insert
	   stat-data while we were doing io, because if it did,
	   insert_by_key() returned error. */
	/* but what _is_ possible is that plugin for inode's stat-data,
	   list of non-standard plugins or their state would change
	   during io, so that stat-data wouldn't fit into sd. To avoid
	   this race we keep inode_state lock. This lock has to be
	   taken each time you access inode in a way that would cause
	   changes in sd size: changing plugins etc.
	*/

	if (result == IBK_INSERT_OK) {
		result = zload(coord.node);
		if (result == 0) {
			/* have we really inserted stat data? */
			assert("nikita-725", item_is_statdata(&coord));

			/* inode was just created. It is inserted into hash
			   table, but no directory entry was yet inserted into
			   parent. So, inode is inaccessible through
			   ->lookup(). All places that directly grab inode
			   from hash-table (like old knfsd), should check
			   IMMUTABLE flag that is set by common_create_child.
			*/
			if (data.iplug && data.iplug->s.sd.save) {
				area = item_body_by_coord(&coord);
				result = data.iplug->s.sd.save(inode, &area);
				if (result == 0) {
					/* object has stat-data now */
					inode_clr_flag(inode, REISER4_NO_SD);
					inode_set_flag(inode, REISER4_SDLEN_KNOWN);
					/* initialise stat-data seal */
					seal_init(&ref->sd_seal, &coord, &key);
					ref->sd_coord = coord;
					check_inode_seal(inode, &coord, &key);
				} else
					result = RETERR(-EIO);
			}
			zrelse(coord.node);
		}
	}
	done_lh(&lh);

	if (result != 0)
		key_warning(&key, result);
	else
		oid_count_allocated();

	return result;
}

static int
update_sd_at(struct inode * inode, coord_t * coord, reiser4_key * key, 
	     lock_handle * lh)
{
	int                result;
	reiser4_item_data  data;
	char              *area;
	reiser4_inode     *state;
	znode             *loaded;

	state = reiser4_inode_data(inode);

	result = zload(coord->node);
	if (result != 0)
		return result;
	loaded = coord->node;

	spin_lock_inode(inode);
	assert("nikita-728", state->pset->sd != NULL);
	data.iplug = state->pset->sd;

	/* data.length is how much space to add to (or remove
	   from if negative) sd */
	if (!inode_get_flag(inode, REISER4_SDLEN_KNOWN)) {
		/* recalculate stat-data length */
		data.length = 
			data.iplug->s.sd.save_len(inode) - 
			item_length_by_coord(coord);
		inode_set_flag(inode, REISER4_SDLEN_KNOWN);
	} else
		data.length = 0;
	spin_unlock_inode(inode);

	/*zrelse(coord->node);*/

	/* if on-disk stat data is of different length than required
	   for this inode, resize it */
	if (0 != data.length) {
		data.data = NULL;
		data.user = 0;
		result = resize_item(coord, &data, key, lh, 0);
		if (result != 0) {
			key_warning(key, result);
			zrelse(loaded);
			return result;
		}
		if (loaded != coord->node) {
			/* resize_item moved coord to another node. Zload it */
			zrelse(loaded);
			result = zload(coord->node);
			if (result != 0)
				return result;
			loaded = coord->node;
		}
	}
	area = item_body_by_coord(coord);
	spin_lock_inode(inode);
	result = data.iplug->s.sd.save(inode, &area);
	znode_make_dirty(coord->node);
	/* re-initialise stat-data seal */
	seal_init(&state->sd_seal, coord, key);
	state->sd_coord = *coord;
	spin_unlock_inode(inode);
	check_inode_seal(inode, coord, key);
	zrelse(loaded);

	return result;
}

/* Update existing stat-data in a tree. Called with inode state locked. Return
   inode state locked. */
static int
update_sd(struct inode *inode /* inode to update sd for */ )
{
	int result;
	reiser4_key key;
	coord_t coord;
	seal_t seal;
	reiser4_inode *state;
	lock_handle lh;

	assert("nikita-726", inode != NULL);

	/* no stat-data, nothing to update?! */
	assert("nikita-726000", !inode_get_flag(inode, REISER4_NO_SD));

	init_lh(&lh);

	state = reiser4_inode_data(inode);
	spin_lock_inode(inode);
	coord = state->sd_coord;
	seal = state->sd_seal;
	spin_unlock_inode(inode);

	if (seal_is_set(&seal)) {
		/* first, try to use seal */
		build_sd_key(inode, &key);
		result = seal_validate(&seal, 
				       &coord, 
				       &key, 
				       LEAF_LEVEL, 
				       &lh, 
				       FIND_EXACT, 
				       ZNODE_WRITE_LOCK, 
				       ZNODE_LOCK_LOPRI);
		if (result == 0)
			check_sd_coord(&coord, &key);
	} else
		result = -EAGAIN;

	if (result != 0) {
		coord_init_zero(&coord);
		result = lookup_sd(inode, ZNODE_WRITE_LOCK, &coord, &lh, &key);
	}
	
	/* we don't want to re-check that somebody didn't remove stat-data
	   while we were doing io, because if it did, lookup_sd returned
	   error. */
	if (result == 0)
		result = update_sd_at(inode, &coord, &key, &lh);
	done_lh(&lh);

	return result;
}

/* save object's stat-data to disk */
int
common_file_save(struct inode *inode /* object to save */)
{
	int result;

	assert("nikita-730", inode != NULL);

	if (inode_get_flag(inode, REISER4_NO_SD))
		/* object doesn't have stat-data yet */
		result = insert_new_sd(inode);
	else
		result = update_sd(inode);
	if ((result != 0) && (result != -ENAMETOOLONG))
		/* Don't issue warnings about "name is too long" */
		warning("nikita-2221", "Failed to save sd for %llu: %i",
			get_inode_oid(inode), result);
	return result;
}

/* checks whether yet another hard links to this object can be added */
int
common_file_can_add_link(const struct inode *object /* object to check */ )
{
	assert("nikita-732", object != NULL);

	/* inode->i_nlink is unsigned int, so just check for integer
	 * overflow */
	return object->i_nlink + 1 != 0;
}


/* space for stat data removal is reserved */
int
common_file_delete_no_reserve(struct inode *inode /* object to remove */ )
{
	int result;

	assert("nikita-1477", inode != NULL);

	if (!inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_key sd_key;

		DQUOT_FREE_INODE(inode);
		DQUOT_DROP(inode);

		build_sd_key(inode, &sd_key);
		result = cut_tree(tree_by_inode(inode), &sd_key, &sd_key);
		if (result == 0) {
			inode_set_flag(inode, REISER4_NO_SD);
			result = oid_release(get_inode_oid(inode));
			if (result == 0)
				oid_count_released();
		}
	} else
		result = 0;
	return result;
}

/* common_file_delete() - delete object stat-data. This is to be used when file deletion turns into stat data removal */
int
common_file_delete(struct inode *inode /* object to remove */ )
{
	int result;

	assert("nikita-1477", inode != NULL);

	if (!inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_block_nr reserve;

		/* grab space which is needed to remove one item from the tree */
		if (reiser4_grab_space_force(reserve = estimate_one_item_removal(tree_by_inode(inode)->height),
					     BA_RESERVED | BA_CAN_COMMIT, "common_file_delete")) {
			warning("nikita-2847", 
				"Cannot delete unnamed sd of %lli. Run fsck", 
				get_inode_oid(inode));
			return RETERR(-ENOSPC);
		}
		result = common_file_delete_no_reserve(inode);
	} else
		result = 0;
	return result;
}

/* common directory consists of two items: stat data and one item containing "." and ".." */
static int common_delete_directory(struct inode *inode)
{
	int result;
	dir_plugin *dplug;

	dplug = inode_dir_plugin(inode);
	assert("vs-1101", dplug && dplug->done);

	/* grab space enough for removing two items */
	if (reiser4_grab_space(2 * estimate_one_item_removal(tree_by_inode(inode)->height), BA_RESERVED | BA_CAN_COMMIT, "common_delete_directory"))
		return RETERR(-ENOSPC);

	result = dplug->done(inode);
	if (!result)
		result = common_file_delete_no_reserve(inode);
	all_grabbed2free("common_delete_directory");
	return result;
}

/* ->set_plug_in_inode() default method. */
static int
common_set_plug(struct inode *object /* inode to set plugin on */ ,
		struct inode *parent /* parent object */ ,
		reiser4_object_create_data * data	/* creational
							 * data */ )
{
	__u64 mask;

	object->i_mode = data->mode;
	/* this should be plugin decision */
	object->i_uid = current->fsuid;
	object->i_mtime = object->i_atime = object->i_ctime = CURRENT_TIME;

	/* support for BSD style group-id assignment. */
	if (reiser4_is_set(object->i_sb, REISER4_BSD_GID))
		object->i_gid = parent->i_gid;
	else if (parent->i_mode & S_ISGID) {
		/* parent directory has sguid bit */
		object->i_gid = parent->i_gid;
		if (S_ISDIR(object->i_mode))
			/* sguid is inherited by sub-directories */
			object->i_mode |= S_ISGID;
	} else
		object->i_gid = current->fsgid;

	/* this object doesn't have stat-data yet */
	inode_set_flag(object, REISER4_NO_SD);
	/* setup inode and file-operations for this inode */
	setup_inode_ops(object, data);
	/* i_nlink is left 1 here as set by new_inode() */
	seal_init(&reiser4_inode_data(object)->sd_seal, NULL, NULL);
	mask = (1 << UNIX_STAT) | (1 << LIGHT_WEIGHT_STAT);
	if (!reiser4_is_set(object->i_sb, REISER4_32_BIT_TIMES))
		mask |= (1 << LARGE_TIMES_STAT);

	scint_pack(&reiser4_inode_data(object)->extmask, mask, GFP_ATOMIC);
	return 0;
}

/* Determine object plugin for @inode based on i_mode.
  
   Most objects in reiser4 file system are controlled by standard object
   plugins: regular file, directory, symlink, fifo, and so on.
  
   For such files we don't explicitly store plugin id in object stat
   data. Rather required plugin is guessed from mode bits, where file "type"
   is encoded (see stat(2)).
*/
int
guess_plugin_by_mode(struct inode *inode	/* object to guess plugins
						 * for */ )
{
	int fplug_id;
	int dplug_id;
	reiser4_inode *info;

	assert("nikita-736", inode != NULL);

	dplug_id = fplug_id = -1;

	switch (inode->i_mode & S_IFMT) {
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		fplug_id = SPECIAL_FILE_PLUGIN_ID;
		break;
	case S_IFLNK:
		fplug_id = SYMLINK_FILE_PLUGIN_ID;
		break;
	case S_IFDIR:
		fplug_id = DIRECTORY_FILE_PLUGIN_ID;
		dplug_id = HASHED_DIR_PLUGIN_ID;
		break;
	default:
		warning("nikita-737", "wrong file mode: %o", inode->i_mode);
		return RETERR(-EIO);
	case S_IFREG:
		fplug_id = UNIX_FILE_PLUGIN_ID;
		break;
	}
	info = reiser4_inode_data(inode);
	plugin_set_file(&info->pset, 
			(fplug_id >= 0) ? file_plugin_by_id(fplug_id) : NULL);
	plugin_set_dir(&info->pset, 
		       (dplug_id >= 0) ? dir_plugin_by_id(dplug_id) : NULL);
	return 0;
}

/* this comon implementation of create estimation function may be used when object creation involves insertion of one item
   (usualy stat data) into tree */
static reiser4_block_nr common_estimate_create(struct inode *object)
{
	return estimate_one_insert_item(tree_by_inode(object)->height);
}

/* this comon implementation of create directory estimation function may be used when directory creation involves
   insertion of two items (usualy stat data and item containing "." and "..") into tree */
static reiser4_block_nr common_estimate_create_dir(struct inode *object)
{
	return 2 * estimate_one_insert_item(tree_by_inode(object)->height);
}

/* ->create method of object plugin */
static int
common_file_create(struct inode *object, struct inode *parent, 
		   reiser4_object_create_data * data)
{
	reiser4_block_nr reserve;
	assert("nikita-744", object != NULL);
	assert("nikita-745", parent != NULL);
	assert("nikita-747", data != NULL);
	assert("nikita-748", inode_get_flag(object, REISER4_NO_SD));

	if (reiser4_grab_space(reserve = 
			       common_estimate_create(object), BA_CAN_COMMIT, "common_file_create")) {
		return RETERR(-ENOSPC);
	}
	return common_file_save(object);
}

/* standard implementation of ->owns_item() plugin method: compare objectids
    of keys in inode and coord */
int
common_file_owns_item(const struct inode *inode	/* object to check
						 * against */ ,
		      const coord_t * coord /* coord to check */ )
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert("nikita-760", inode != NULL);
	assert("nikita-761", coord != NULL);

	return			/*coord_is_in_node( coord ) && */
	    coord_is_existing_item(coord) &&
	    (get_key_objectid(build_sd_key(inode, &file_key)) == get_key_objectid(item_key_by_coord(coord, &item_key)));
}

/* @count bytes of flow @f got written, update correspondingly f->length,
   f->data and f->key */
void
move_flow_forward(flow_t * f, unsigned count)
{
	if (f->data)
		f->data += count;
	f->length -= count;
	set_key_offset(&f->key, get_key_offset(&f->key) + count);
}

/* Default method to construct flow into @f according to user-supplied
   data. */
int
common_build_flow(struct inode *inode /* file to build flow for */ ,
		  char *buf /* user level buffer */ ,
		  int user	/* 1 if @buf is of user space, 0 - if it is
				   kernel space */ ,
		  size_t size /* buffer size */ ,
		  loff_t off /* offset to start io from */ ,
		  rw_op op /* READ or WRITE */ ,
		  flow_t * f /* resulting flow */ )
{
	file_plugin *fplug;

	assert("nikita-1100", inode != NULL);

	f->length = size;
	f->data = buf;
	f->user = user;
	f->op = op;
	fplug = inode_file_plugin(inode);
	assert("nikita-1931", fplug != NULL);
	assert("nikita-1932", fplug->key_by_inode != NULL);
	return fplug->key_by_inode(inode, off, &f->key);
}

static int
unix_key_by_inode(struct inode *inode, loff_t off, reiser4_key * key)
{
	build_sd_key(inode, key);
	set_key_type(key, KEY_BODY_MINOR);
	set_key_offset(key, (__u64) off);
	return 0;
}

/* default ->add_link() method of file plugin */
static int
common_add_link(struct inode *object, struct inode *parent UNUSED_ARG)
{
	INODE_INC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* default ->rem_link() method of file plugin */
static int
common_rem_link(struct inode *object, struct inode *parent UNUSED_ARG)
{
	assert("nikita-2021", object != NULL);
	assert("nikita-2163", object->i_nlink > 0);

	INODE_DEC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* ->not_linked() method for file plugins */
static int
common_not_linked(const struct inode *inode)
{
	assert("nikita-2007", inode != NULL);
	return (inode->i_nlink == 0);
}

/* ->not_linked() method the for directory file plugin */
static int
dir_not_linked(const struct inode *inode)
{
	assert("nikita-2008", inode != NULL);
	/* one link from dot */
	return (inode->i_nlink == 1);
}

/* ->adjust_to_parent() method for regular files */
static int
common_adjust_to_parent(struct inode *object /* new object */ ,
			struct inode *parent /* parent directory */ ,
			struct inode *root /* root directory */ )
{
	reiser4_inode *self;
	reiser4_inode *ancestor;

	assert("nikita-2165", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2069", parent != NULL);

	self = reiser4_inode_data(object);
	ancestor = reiser4_inode_data(parent);

	grab_plugin(self, ancestor, file);
	grab_plugin(self, ancestor, sd);
	grab_plugin(self, ancestor, tail);
	grab_plugin(self, ancestor, perm);
	return 0;
}

/* ->adjust_to_parent() method for directory files */
static int
dir_adjust_to_parent(struct inode *object /* new object */ ,
		     struct inode *parent /* parent directory */ ,
		     struct inode *root /* root directory */ )
{
	reiser4_inode *self;
	reiser4_inode *ancestor;

	assert("nikita-2166", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2167", parent != NULL);

	self = reiser4_inode_data(object);
	ancestor = reiser4_inode_data(parent);

	grab_plugin(self, ancestor, file);
	grab_plugin(self, ancestor, dir);
	grab_plugin(self, ancestor, sd);
	grab_plugin(self, ancestor, hash);
	grab_plugin(self, ancestor, tail);
	grab_plugin(self, ancestor, perm);
	grab_plugin(self, ancestor, dir_item);
	return 0;
}

/* simplest implementation of ->getattr() method. Completely static. */
static int
common_getattr(struct vfsmount *mnt UNUSED_ARG, struct dentry *dentry, struct kstat *stat)
{
	struct inode *obj;

	assert("nikita-2298", dentry != NULL);
	assert("nikita-2299", stat != NULL);
	assert("nikita-2300", dentry->d_inode != NULL);

	obj = dentry->d_inode;

	stat->dev = obj->i_sb->s_dev;
	stat->ino = oid_to_uino(get_inode_oid(obj));
	stat->mode = obj->i_mode;
	/* don't confuse userland with huge nlink. This is not entirely
	 * correct, because nlink_t is not necessary 16 bit signed. */
	stat->nlink = min(obj->i_nlink, (typeof(obj->i_nlink))0x7fff);
	stat->uid = obj->i_uid;
	stat->gid = obj->i_gid;
	stat->rdev = kdev_t_to_nr(obj->i_rdev);
	stat->atime = obj->i_atime;
	stat->mtime = obj->i_mtime;
	stat->ctime = obj->i_ctime;
	stat->size = obj->i_size;
	stat->blocks = (inode_get_bytes(obj) + VFS_BLKSIZE) >> VFS_BLKSIZE_BITS;
	/* "preferred" blocksize for efficient file system I/O */
	stat->blksize = get_super_private(obj->i_sb)->optimal_io_size;

	return 0;
}

/* plugin->u.file.release */
static int
dir_release(struct file *file)
{
	if (file->private_data != NULL)
		readdir_list_remove(reiser4_get_file_fsdata(file));
	return 0;
}

static loff_t
dir_seek(struct file *file, loff_t off, int origin)
{
	loff_t result;

	trace_on(TRACE_DIR | TRACE_VFS_OPS, "dir_seek: %s: %lli -> %lli/%i\n",
		 file->f_dentry->d_name.name, file->f_pos, off, origin);
	result = default_llseek(file, off, origin);
	if (result >= 0) {
		int ff;
		coord_t coord;
		lock_handle lh;
		tap_t tap;
		readdir_pos *pos;

		coord_init_zero(&coord);
		init_lh(&lh);
		tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

		ff = dir_readdir_init(file, &tap, &pos);
		if (ff != 0)
			result = (loff_t) ff;
		tap_done(&tap);
	}
	return result;
}

/* default implementation of ->bind() method of file plugin */
static int
common_bind(struct inode *child UNUSED_ARG, struct inode *parent UNUSED_ARG)
{
	return 0;
}

#define common_detach common_bind

static int
dir_detach(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2883", dplug != NULL);
	assert("nikita-2884", dplug->detach != NULL);
	return dplug->detach(child, parent);
}


/* this common implementation of update estimation function may be used when stat data update does not do more than
   inserting a unit into a stat data item which is probably true for most cases */
static reiser4_block_nr 
common_estimate_update(const struct inode *inode)
{
	return estimate_one_insert_into_item(tree_by_inode(inode)->height);
}

static reiser4_block_nr 
common_estimate_unlink(struct inode *object, struct inode *parent)
{
	return 0;
}

static reiser4_block_nr 
dir_estimate_unlink(struct inode *object, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(object);
	assert("nikita-2888", dplug != NULL);
	assert("nikita-2887", dplug->estimate.unlink != NULL);
	return dplug->estimate.unlink(object, parent);
}

/* implementation of ->bind() method for file plugin of directory file */
static int
dir_bind(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2646", dplug != NULL);
	return dplug->attach(child, parent);
}

int
common_setattr(struct inode *inode /* Object to change attributes */,
	       struct iattr *attr /* change description */)
{
	int   result;
	__u64 tograb;

	assert("nikita-3119", !(attr->ia_valid & ATTR_SIZE));

	tograb = estimate_one_insert_into_item(tree_by_inode(inode)->height);
	result = reiser4_grab_space(tograb, BA_CAN_COMMIT, __FUNCTION__);
	if (!result) {
		result = inode_setattr(inode, attr);
		if (!result)
			/* "capture" inode */
			result = reiser4_mark_inode_dirty(inode);
		all_grabbed2free(__FUNCTION__);
	}
	return result;
}

static ssize_t
isdir(void)
{
	return RETERR(-EISDIR);
}

#define eisdir ((void *)isdir)

static ssize_t
perm(void)
{
	return RETERR(-EPERM);
}

#define eperm ((void *)perm)

file_plugin file_plugins[LAST_FILE_PLUGIN_ID] = {
	[UNIX_FILE_PLUGIN_ID] = {
				    .h = {
					  .type_id = REISER4_FILE_PLUGIN_TYPE,
					  .id = UNIX_FILE_PLUGIN_ID,
					  .pops = NULL,
					  .label = "reg",
					  .desc = "regular file",
					  .linkage = TS_LIST_LINK_ZERO
				    },
				    .truncate = unix_file_truncate,
				    .write_sd_by_inode = common_file_save,
				    .readpage = unix_file_readpage,
				    .writepage = unix_file_writepage,
				    .read = unix_file_read,
				    .write = unix_file_write,
				    .release = unix_file_release,
				    .ioctl = unix_file_ioctl,
				    .mmap = unix_file_mmap,
				    .get_block = unix_file_get_block,
				    .flow_by_inode = common_build_flow /*NULL*/,
				    .key_by_inode = unix_key_by_inode,
				    .set_plug_in_inode = common_set_plug,
				    .adjust_to_parent = common_adjust_to_parent,
				    .create = common_file_create,
				    .delete = unix_file_delete,
				    .add_link = common_add_link,
				    .rem_link = common_rem_link,
				    .owns_item = unix_file_owns_item,
				    .can_add_link = common_file_can_add_link,
				    .can_rem_link = NULL,
				    .not_linked = common_not_linked,
				    .setattr = unix_file_setattr,
				    .getattr = common_getattr,
				    .seek = NULL,
				    .detach = common_detach,
				    .bind = common_bind,
				    .estimate = {
					    .create = common_estimate_create,
					    .update = common_estimate_update,
					    .unlink = common_estimate_unlink
				    },
				    .readpages = unix_file_readpages,
				    .init_inode_data = unix_file_init_inode,
				    .pre_delete = unix_file_pre_delete
	},
	[DIRECTORY_FILE_PLUGIN_ID] = {
				      .h = {
					    .type_id = REISER4_FILE_PLUGIN_TYPE,
					    .id = DIRECTORY_FILE_PLUGIN_ID,
					    .pops = NULL,
					    .label = "dir",
					    .desc = "directory",
					    .linkage = TS_LIST_LINK_ZERO},
				      .truncate = eisdir,
				      .write_sd_by_inode = common_file_save,
				      .readpage = eisdir,
				      .writepage = eisdir,
				      .read = eisdir,
				      .write = eisdir,
				      .release = dir_release,
				      .ioctl = eisdir,
				      .mmap = eisdir,
				      .get_block = NULL,
				      .flow_by_inode = NULL,
				      .key_by_inode = NULL,
				      .set_plug_in_inode = common_set_plug,
				      .adjust_to_parent = dir_adjust_to_parent,
				      .create = common_file_create,
				      .delete = common_delete_directory,
				      .add_link = common_add_link,
				      .rem_link = common_rem_link,
				      .owns_item = hashed_owns_item,
				      .can_add_link = common_file_can_add_link,
				      .can_rem_link = is_dir_empty,
				      .not_linked = dir_not_linked,
				      .setattr = common_setattr,
				      .getattr = common_getattr,
				      .seek = dir_seek,
				      .detach = dir_detach,
				      .bind = dir_bind,
				      .estimate = {
					    .create = common_estimate_create_dir,
					    .update = common_estimate_update,
					    .unlink = dir_estimate_unlink
				      },
				      .readpages = NULL,
				      .init_inode_data = NULL,
				      .pre_delete = NULL
	},
	[SYMLINK_FILE_PLUGIN_ID] = {
				    .h = {
					  .type_id = REISER4_FILE_PLUGIN_TYPE,
					  .id = SYMLINK_FILE_PLUGIN_ID,
					  .pops = NULL,
					  .label = "symlink",
					  .desc = "symbolic link",
					  .linkage = TS_LIST_LINK_ZERO}
				    ,
				    .truncate = eperm,
				    .write_sd_by_inode = common_file_save,
				    .readpage = eperm,
				    .writepage = eperm,
				    .read = eperm,
				    .write = eperm,
				    .release = NULL,
				    .ioctl = eperm,
				    .mmap = eperm,
				    .get_block = NULL,
				    .flow_by_inode = NULL,
				    .key_by_inode = NULL,
				    .set_plug_in_inode = common_set_plug,
				    .adjust_to_parent = common_adjust_to_parent,
				    .create = symlink_create,
				    /* FIXME-VS: symlink should probably have its own destroy method */
				    .delete = common_file_delete,
				    .add_link = common_add_link,
				    .rem_link = common_rem_link,
				    .owns_item = NULL,
				    .can_add_link = common_file_can_add_link,
				    .can_rem_link = NULL,
				    .not_linked = common_not_linked,
				    .setattr = common_setattr,
				    .getattr = common_getattr,
				    .seek = NULL,
				    .detach = common_detach,
				    .bind = common_bind,
				    .estimate = {
					    .create = common_estimate_create,
					    .update = common_estimate_update,
					    .unlink = common_estimate_unlink
				    },
				    .readpages = NULL,
				    .init_inode_data = NULL,
				    .pre_delete = NULL
	},
	[SPECIAL_FILE_PLUGIN_ID] = {
				    .h = {
					  .type_id = REISER4_FILE_PLUGIN_TYPE,
					  .id = SPECIAL_FILE_PLUGIN_ID,
					  .pops = NULL,
					  .label = "special",
					  .desc = "special: fifo, device or socket",
					  .linkage = TS_LIST_LINK_ZERO}
				    ,
				    .truncate = eperm,
				    .create = common_file_create,
				    .write_sd_by_inode = common_file_save,
				    .readpage = eperm,
				    .writepage = eperm,
				    .read = eperm,
				    .write = eperm,
				    .release = NULL,
				    .ioctl = eperm,
				    .mmap = eperm,
				    .get_block = NULL,
				    .flow_by_inode = NULL,
				    .key_by_inode = NULL,
				    .set_plug_in_inode = common_set_plug,
				    .adjust_to_parent = common_adjust_to_parent,
				    .delete = common_file_delete,
				    .add_link = common_add_link,
				    .rem_link = common_rem_link,
				    .owns_item = common_file_owns_item,
				    .can_add_link = common_file_can_add_link,
				    .can_rem_link = NULL,
				    .not_linked = common_not_linked,
				    .setattr = common_setattr,
				    .getattr = common_getattr,
				    .seek = NULL,
				    .detach = common_detach,
				    .bind = common_bind,
				    .estimate = {
					    .create = common_estimate_create,
					    .update = common_estimate_update,
					    .unlink = common_estimate_unlink
				    },
				    .readpages = NULL,
				    .init_inode_data = NULL,
				    .pre_delete = NULL
	},
	[PSEUDO_FILE_PLUGIN_ID] = {
				    .h = {
					  .type_id = REISER4_FILE_PLUGIN_TYPE,
					  .id = PSEUDO_FILE_PLUGIN_ID,
					  .pops = NULL,
					  .label = "pseudo",
					  .desc = "pseudo file",
					  .linkage = TS_LIST_LINK_ZERO
				    },
#if 0
				    .truncate          = ,
				    .write_sd_by_inode = ,
				    .readpage          = ,
				    .writepage         = ,
				    .read              = ,
				    .write             = ,
				    .release           = ,
				    .ioctl             = ,
				    .mmap              = ,
				    .get_block         = ,
				    .flow_by_inode     = ,
				    .key_by_inode      = ,
				    .set_plug_in_inode = ,
				    .adjust_to_parent  = ,
				    .create            = ,
				    .delete            = ,
				    .add_link          = ,
				    .rem_link          = ,
				    .owns_item         = ,
				    .can_add_link      = ,
				    .can_rem_link      = ,
				    .not_linked        = ,
				    .setattr           = ,
				    .getattr           = ,
				    .seek              = ,
				    .detach            = ,
				    .bind              = ,
				    .estimate = {
					    .create = ,
					    .update = ,
					    .unlink = 
				    },
				    .readpages = ,
				    .init_inode_data,
				    .pre_delete = NULL
#endif
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
