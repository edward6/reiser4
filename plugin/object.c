/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

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
#include "file/pseudo.h"
#include "symlink.h"
#include "dir/dir.h"
#include "item/item.h"
#include "plugin.h"
#include "object.h"
#include "../znode.h"
#include "../tap.h"
#include "../tree.h"
#include "../vfs_ops.h"
#include "../inode.h"
#include "../super.h"
#include "../reiser4.h"
#include "../prof.h"

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/quotaops.h>
#include <linux/security.h> /* security_inode_delete() */

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
check_sd_coord(coord_t *coord, const reiser4_key *key)
{
	reiser4_key ukey;

	coord_clear_iplug(coord);
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
	  const reiser4_key * key /* resulting key */ )
{
	int result;
	__u32 flags;

	assert("nikita-1692", inode != NULL);
	assert("nikita-1693", coord != NULL);
	assert("nikita-1694", key != NULL);

	/* look for the object's stat data in a tree.
	   This returns in "node" pointer to a locked znode and in "pos"
	   position of an item found in node. Both are only valid if
	   coord_found is returned. */
	flags = (lock_mode == ZNODE_WRITE_LOCK) ? CBK_FOR_INSERT : 0;
	flags |= CBK_UNIQUE;
	result = coord_by_key(tree_by_inode(inode),
			      key,
			      coord,
			      lh,
			      lock_mode,
			      FIND_EXACT,
			      LEAF_LEVEL,
			      LEAF_LEVEL,
			      flags,
			      0);
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
	oid = oid_allocate(inode->i_sb);
	if (oid == ABSOLUTE_MAX_OID)
		return RETERR(-EOVERFLOW);

	set_inode_oid(inode, oid);

	coord_init_zero(&coord);
	init_lh(&lh);

	result = insert_by_key(tree_by_inode(inode),
			       build_sd_key(inode, &key),
			       &data,
			       &coord,
			       &lh,
			       /* stat data lives on a leaf level */
			       LEAF_LEVEL,
			       inter_syscall_ra(inode),
			       NO_RAP,
			       CBK_UNIQUE);

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
		coord_clear_iplug(&coord);
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
			assert("nikita-3240", data.iplug != NULL);
			assert("nikita-3241", data.iplug->s.sd.save != NULL);
			area = item_body_by_coord(&coord);
			result = data.iplug->s.sd.save(inode, &area);
			znode_make_dirty(coord.node);
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

	coord_clear_iplug(coord);
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
			coord_clear_iplug(coord);
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

#define SEAL_VALIDATE \
{\
	PROF_BEGIN(seal_validate);\
	if (seal_is_set(&seal)) {\
		/* first, try to use seal */\
		build_sd_key(inode, &key);\
		result = seal_validate(&seal,\
				       &coord,\
				       &key,\
				       LEAF_LEVEL,\
				       &lh,\
				       FIND_EXACT,\
				       ZNODE_WRITE_LOCK,\
				       ZNODE_LOCK_LOPRI);\
		if (result == 0)\
			check_sd_coord(&coord, &key);\
	} else\
		result = -E_REPEAT;\
	PROF_END(seal_validate);\
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
	coord_clear_iplug(&coord);
	seal = state->sd_seal;
	spin_unlock_inode(inode);

	build_sd_key(inode, &key);
	if (seal_is_set(&seal)) {
		/* first, try to use seal */
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
		result = -E_REPEAT;

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
write_sd_by_inode_common(struct inode *inode /* object to save */)
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
can_add_link_common(const struct inode *object /* object to check */ )
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
			result = oid_release(inode->i_sb, get_inode_oid(inode));
			if (result == 0)
				oid_count_released();
		}
	} else
		result = 0;
	return result;
}

/* delete_file_common() - delete object stat-data. This is to be used when file deletion turns into stat data removal */
int
delete_file_common(struct inode *inode /* object to remove */ )
{
	int result;

	assert("nikita-1477", inode != NULL);

	if (!inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_block_nr reserve;

		/* grab space which is needed to remove one item from the tree */
		reserve = estimate_one_item_removal(tree_by_inode(inode));
		if (reiser4_grab_space_force(reserve,
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
static int delete_directory_common(struct inode *inode)
{
	int result;
	dir_plugin *dplug;

	dplug = inode_dir_plugin(inode);
	assert("vs-1101", dplug && dplug->done);

	/* grab space enough for removing two items */
	if (reiser4_grab_space(2 * estimate_one_item_removal(tree_by_inode(inode)), BA_RESERVED | BA_CAN_COMMIT, "common_delete_directory"))
		return RETERR(-ENOSPC);

	result = dplug->done(inode);
	if (!result)
		result = common_file_delete_no_reserve(inode);
	all_grabbed2free("common_delete_directory");
	return result;
}

/* ->set_plug_in_inode() default method. */
static int
set_plug_in_inode_common(struct inode *object /* inode to set plugin on */ ,
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
static reiser4_block_nr estimate_create_file_common(struct inode *object)
{
	return estimate_one_insert_item(tree_by_inode(object));
}

/* this comon implementation of create directory estimation function may be used when directory creation involves
   insertion of two items (usualy stat data and item containing "." and "..") into tree */
static reiser4_block_nr estimate_create_dir_common(struct inode *object)
{
	return 2 * estimate_one_insert_item(tree_by_inode(object));
}

/* ->create method of object plugin */
static int
create_common(struct inode *object, struct inode *parent UNUSED_ARG,
	      reiser4_object_create_data * data UNUSED_ARG)
{
	reiser4_block_nr reserve;
	assert("nikita-744", object != NULL);
	assert("nikita-745", parent != NULL);
	assert("nikita-747", data != NULL);
	assert("nikita-748", inode_get_flag(object, REISER4_NO_SD));

	if (reiser4_grab_space(reserve =
			       estimate_create_file_common(object), BA_CAN_COMMIT, "common_file_create")) {
		return RETERR(-ENOSPC);
	}
	return write_sd_by_inode_common(object);
}

/* standard implementation of ->owns_item() plugin method: compare objectids
    of keys in inode and coord */
int
owns_item_common(const struct inode *inode	/* object to check
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

/* default ->add_link() method of file plugin */
static int
add_link_common(struct inode *object, struct inode *parent UNUSED_ARG)
{
	INODE_INC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* default ->rem_link() method of file plugin */
static int
rem_link_common(struct inode *object, struct inode *parent UNUSED_ARG)
{
	assert("nikita-2021", object != NULL);
	assert("nikita-2163", object->i_nlink > 0);

	INODE_DEC_FIELD(object, i_nlink);
	object->i_ctime = CURRENT_TIME;
	return 0;
}

/* ->not_linked() method for file plugins */
static int
not_linked_common(const struct inode *inode)
{
	assert("nikita-2007", inode != NULL);
	return (inode->i_nlink == 0);
}

/* ->not_linked() method the for directory file plugin */
static int
not_linked_dir(const struct inode *inode)
{
	assert("nikita-2008", inode != NULL);
	/* one link from dot */
	return (inode->i_nlink == 1);
}

/* ->adjust_to_parent() method for regular files */
static int
adjust_to_parent_common(struct inode *object /* new object */ ,
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
adjust_to_parent_dir(struct inode *object /* new object */ ,
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
getattr_common(struct vfsmount *mnt UNUSED_ARG, struct dentry *dentry, struct kstat *stat)
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
	stat->rdev = obj->i_rdev;
	stat->atime = obj->i_atime;
	stat->mtime = obj->i_mtime;
	stat->ctime = obj->i_ctime;
	stat->size = obj->i_size;
	stat->blocks = (inode_get_bytes(obj) + VFS_BLKSIZE - 1) >> VFS_BLKSIZE_BITS;
	/* "preferred" blocksize for efficient file system I/O */
	stat->blksize = get_super_private(obj->i_sb)->optimal_io_size;

	return 0;
}

/* plugin->u.file.release */
static int
release_dir(struct inode *inode, struct file *file)
{
	spin_lock_inode(inode);
	if (file->private_data != NULL)
		readdir_list_remove(reiser4_get_file_fsdata(file));
	spin_unlock_inode(inode);
	return 0;
}

static loff_t
seek_dir(struct file *file, loff_t off, int origin)
{
	loff_t result;
	struct inode *inode;

	inode = file->f_dentry->d_inode;
	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "dir_seek: %s: %lli -> %lli/%i\n",
		 file->f_dentry->d_name.name, file->f_pos, off, origin);
	down(&inode->i_sem);
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
	up(&inode->i_sem);
	return result;
}

/* default implementation of ->bind() method of file plugin */
static int
bind_common(struct inode *child UNUSED_ARG, struct inode *parent UNUSED_ARG)
{
	return 0;
}

#define detach_common bind_common
#define cannot ((void *)bind_common)

static int
detach_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2883", dplug != NULL);
	assert("nikita-2884", dplug->detach != NULL);
	return dplug->detach(child, parent);
}


/* this common implementation of update estimation function may be used when stat data update does not do more than
   inserting a unit into a stat data item which is probably true for most cases */
reiser4_block_nr
estimate_update_common(const struct inode *inode)
{
	return estimate_one_insert_into_item(tree_by_inode(inode));
}

static reiser4_block_nr
estimate_unlink_common(struct inode *object UNUSED_ARG,
		       struct inode *parent UNUSED_ARG)
{
	return 0;
}

static reiser4_block_nr
estimate_unlink_dir_common(struct inode *object, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(object);
	assert("nikita-2888", dplug != NULL);
	assert("nikita-2887", dplug->estimate.unlink != NULL);
	return dplug->estimate.unlink(object, parent);
}

/* implementation of ->bind() method for file plugin of directory file */
static int
bind_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2646", dplug != NULL);
	return dplug->attach(child, parent);
}

int
setattr_common(struct inode *inode /* Object to change attributes */,
	       struct iattr *attr /* change description */)
{
	int   result;
	__u64 tograb;

	assert("nikita-3119", !(attr->ia_valid & ATTR_SIZE));

	tograb = estimate_one_insert_into_item(tree_by_inode(inode));
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

static void drop_object_body(struct inode *inode)
{
	if (!inode_file_plugin(inode)->pre_delete)
		return;
	if (inode_file_plugin(inode)->pre_delete(inode))
		warning("vs-1216", "Failed to delete file body for %llu)\n",
			get_inode_oid(inode));
}

/* doesn't seem to be exported in headers. */
extern spinlock_t inode_lock;

static void delete_inode_common(struct inode *object)
{
	/* create context here.

	removal of inode from the hash table (done at the very beginning of
	generic_delete_inode(), truncate of pages, and removal of file's
	extents has to be performed in the same atom. Otherwise, it may so
	happen, that twig node with unallocated extent will be flushed to the
	disk.
	*/
	reiser4_context ctx;

	/*
	 * FIXME: this resembles generic_delete_inode
	 */
	hlist_del_init(&object->i_hash);
	list_del_init(&object->i_list);
	object->i_state|=I_FREEING;
	inodes_stat.nr_inodes--;
	spin_unlock(&inode_lock);

	init_context(&ctx, object->i_sb);

	uncapture_inode(object);

	if (!is_bad_inode(object)) {
		drop_object_body(object);
		assert("vs-1430", reiser4_inode_data(object)->jnodes == 0);
	}

	if (object->i_data.nrpages) {
		warning("vs-1434", "nrpages %ld\n", object->i_data.nrpages);
		truncate_inode_pages(&object->i_data, 0);
	}

	security_inode_delete(object);
	if (!is_bad_inode(object))
		DQUOT_INIT(object);

	object->i_sb->s_op->delete_inode(object);
	if (object->i_state != I_CLEAR)
		BUG();
	destroy_inode(object);
	(void)reiser4_exit_context(&ctx);
}

static void forget_inode_common(struct inode *object)
{
	generic_forget_inode(object);
}

static void drop_common(struct inode * object)
{
	file_plugin *fplug;

	assert("nikita-2643", object != NULL);

	/* -not- creating context in this method, because it is frequently
	   called and all existing ->not_linked() methods are one liners. */

	fplug = inode_file_plugin(object);
	/* fplug is NULL for fake inode */
	if (fplug != NULL && fplug->not_linked(object)) {
		assert("nikita-3231", fplug->delete_inode != NULL);
		fplug->delete_inode(object);
	} else {
		assert("nikita-3232", fplug->forget_inode != NULL);
		fplug->forget_inode(object);
	}
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
		/* FIXME: check which of these are relly needed */
		.open = NULL,
		.truncate = truncate_unix_file,
		.write_sd_by_inode = write_sd_by_inode_common,
		.readpage = readpage_unix_file,
		.capture = capture_unix_file,
		.read = read_unix_file,
		.write = write_unix_file,
		.release = release_unix_file,
		.ioctl = ioctl_unix_file,
		.mmap = mmap_unix_file,
		.get_block = get_block_unix_file,
		.flow_by_inode = flow_by_inode_unix_file,
		.key_by_inode = key_by_inode_unix_file,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_common,
		.delete = delete_unix_file,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_unix_file,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_unix_file,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.readpages = readpages_unix_file,
		.init_inode_data = init_inode_data_unix_file,
		.pre_delete = pre_delete_unix_file,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.forget_inode = forget_inode_common
	},
	[DIRECTORY_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = DIRECTORY_FILE_PLUGIN_ID,
			.pops = NULL,
			.label = "dir",
			.desc = "directory",
			.linkage = TS_LIST_LINK_ZERO},
		.open = NULL,
		.truncate = eisdir,
		.write_sd_by_inode = write_sd_by_inode_common,/*common_file_save,*/
		.readpage = eisdir,
		.capture = NULL,
		.read = eisdir,
		.write = eisdir,
		.release = release_dir,
		.ioctl = eisdir,
		.mmap = eisdir,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,/*common_set_plug,*/
		.adjust_to_parent = adjust_to_parent_dir,
		.create = create_common, /*common_file_create,*/
		.delete = delete_directory_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_hashed,
		.can_add_link = can_add_link_common,
		.can_rem_link = is_dir_empty,
		.not_linked = not_linked_dir,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = seek_dir,
		.detach = detach_dir,
		.bind = bind_dir,
		.estimate = {
			.create = estimate_create_dir_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_dir_common
		},
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.forget_inode = forget_inode_common
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
		.open = NULL,
		.truncate = eperm,
		.write_sd_by_inode = write_sd_by_inode_common,
		.readpage = eperm,
		.capture = NULL,
		.read = eperm,
		.write = eperm,
		.release = NULL,
		.ioctl = eperm,
		.mmap = eperm,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,/*common_set_plug,*/
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_symlink,
		/* FIXME-VS: symlink should probably have its own destroy method */
		.delete = delete_file_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = NULL,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.forget_inode = forget_inode_common
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
		.open = NULL,
		.truncate = eperm,
		.create = create_common,
		.write_sd_by_inode = write_sd_by_inode_common,
		.readpage = eperm,
		.capture = NULL,
		.read = eperm,
		.write = eperm,
		.release = NULL,
		.ioctl = eperm,
		.mmap = eperm,
		.get_block = NULL,
		.flow_by_inode = NULL,
		.key_by_inode = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.delete = delete_file_common,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_common,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.readpages = NULL,
		.init_inode_data = init_inode_ordering,
		.pre_delete = NULL,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.forget_inode = forget_inode_common	
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
		.open =              open_pseudo,
		.truncate          = eperm,
		.write_sd_by_inode = eperm,
		.readpage          = eperm,
		.capture           = NULL,
		.read              = read_pseudo,
		.write             = write_pseudo,
		.release           = release_pseudo,
		.ioctl             = eperm,
		.mmap              = eperm,
		.get_block         = eperm,
		.flow_by_inode     = NULL,
		.key_by_inode      = NULL,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent  = NULL,
		.create            = NULL,
		.delete            = eperm,
		.add_link          = NULL,
		.rem_link          = NULL,
		.owns_item         = NULL,
		.can_add_link      = cannot,
		.can_rem_link      = cannot,
		.not_linked        = NULL,
		.setattr           = inode_setattr,
		.getattr           = getattr_common,
		.seek              = seek_pseudo,
		.detach            = detach_common,
		.bind              = bind_common,
		.estimate = {
			.create = NULL,
			.update = NULL,
			.unlink = NULL
		},
		.readpages = NULL,
		.init_inode_data = NULL,
		.pre_delete = NULL,
		.drop = drop_pseudo,
		.delete_inode = NULL,
		.forget_inode = NULL
	},
	[CRC_FILE_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_FILE_PLUGIN_TYPE,
			.id = CRC_FILE_PLUGIN_ID,
			.pops = NULL,
			.label = "cryptcompress",
			.desc = "cryptcompress file",
			.linkage = TS_LIST_LINK_ZERO
		},
		/* FIXME: check which of these are relly needed */
		.open = NULL,
		.truncate = truncate_cryptcompress,
		.write_sd_by_inode = write_sd_by_inode_common,
		.readpage = readpage_cryptcompress,
		.capture = capture_cryptcompress,
		.read = generic_file_read,
		.write = write_cryptcompress,
		.release = NULL,
		.ioctl = NULL,
		.mmap = generic_file_mmap,
		.get_block = get_block_cryptcompress,
		.flow_by_inode = flow_by_inode_cryptcompress,
		.key_by_inode = key_by_inode_cryptcompress,
		.set_plug_in_inode = set_plug_in_inode_common,
		.adjust_to_parent = adjust_to_parent_common,
		.create = create_cryptcompress,
		.delete = delete_cryptcompress,
		.add_link = add_link_common,
		.rem_link = rem_link_common,
		.owns_item = owns_item_common,
		.can_add_link = can_add_link_common,
		.can_rem_link = NULL,
		.not_linked = not_linked_common,
		.setattr = setattr_cryptcompress,
		.getattr = getattr_common,
		.seek = NULL,
		.detach = detach_common,
		.bind = bind_common,
		.estimate = {
			.create = estimate_create_file_common,
			.update = estimate_update_common,
			.unlink = estimate_unlink_common
		},
		.readpages = readpages_cryptcompress,
		.init_inode_data = NULL,
		.pre_delete = pre_delete_cryptcompress,
		.drop = drop_common,
		.delete_inode = delete_inode_common,
		.forget_inode = forget_inode_common
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
