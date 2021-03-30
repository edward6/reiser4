/* Copyright 2005 by Hans Reiser, licensing governed by
   reiser4/README */

/* this file contains typical implementations for most of methods of
   file plugin
*/

#include "../inode.h"
#include "object.h"
#include "../safe_link.h"

static int insert_new_sd(struct inode *inode, oid_t oid);
static int update_sd(struct inode *inode);

void build_body_key_common(struct inode *inode, reiser4_key *key)
{
	reiser4_key_init(key);
	set_key_locality(key, reiser4_inode_data(inode)->locality_id);
	set_key_objectid(key, get_inode_oid(inode));
	set_key_type(key, KEY_BODY_MINOR);
}

/**
 * Common implementation of ->write_sd_by_inode() of file plugins.
 * Either insert stat-data or update it.
 * @inode: object to write stat-data of
 */
int write_sd_by_inode_common(struct inode *inode, oid_t *oid)
{
	int result;

	assert("nikita-730", inode != NULL);

	if (reiser4_inode_get_flag(inode, REISER4_NO_SD)) {
		/*
		 * object doesn't have stat-data yet
		 */
		assert("edward-1785", oid != NULL);
		result = insert_new_sd(inode, *oid);
	}
	else
		assert("edward-1786", oid == NULL);
		result = update_sd(inode);

		if (result != 0 &&
		    result != -ENAMETOOLONG &&
		    result != -ENOMEM)
			/*
			 * Don't issue warnings about "name is too long"
			 */
			warning("nikita-2221",
				"Failed to save sd for %llu: %i",
				(unsigned long long)get_inode_oid(inode),
				result);
		return result;
}

/* this is common implementation of set_plug_in_inode method of file plugin
 */
int set_plug_in_inode_common(struct inode *object /* inode to set plugin on */ ,
			     struct inode *parent /* parent object */ ,
			     reiser4_object_create_data * data	/* creational
								 * data */ )
{
	__u64 mask;

	object->i_mode = data->mode;
	/* this should be plugin decision */
	object->i_uid = current_fsuid();
	object->i_mtime = object->i_atime = object->i_ctime = current_time(object);

	/* support for BSD style group-id assignment. See mount's manual page
	   description of bsdgroups ext2 mount options for more details */
	if (reiser4_is_set(object->i_sb, REISER4_BSD_GID))
		object->i_gid = parent->i_gid;
	else if (parent->i_mode & S_ISGID) {
		/* parent directory has sguid bit */
		object->i_gid = parent->i_gid;
		if (S_ISDIR(object->i_mode))
			/* sguid is inherited by sub-directories */
			object->i_mode |= S_ISGID;
	} else
		object->i_gid = current_fsgid();

	/* this object doesn't have stat-data yet */
	reiser4_inode_set_flag(object, REISER4_NO_SD);
#if 0
	/* this is now called after all inode plugins are initialized:
	   do_create_vfs_child after adjust_to_parent */
	/* setup inode and file-operations for this inode */
	setup_inode_ops(object, data);
#endif
	reiser4_seal_init(&reiser4_inode_data(object)->sd_seal, NULL, NULL);
	mask = (1 << UNIX_STAT) | (1 << LIGHT_WEIGHT_STAT);
	if (!reiser4_is_set(object->i_sb, REISER4_32_BIT_TIMES))
		mask |= (1 << LARGE_TIMES_STAT);

	reiser4_inode_data(object)->extmask = mask;
	return 0;
}

/* this is common implementation of adjust_to_parent method of file plugin for
   regular files
 */
int adjust_to_parent_common(struct inode *object /* new object */ ,
			    struct inode *parent /* parent directory */ ,
			    struct inode *root/* root directory */)
{
	assert("nikita-2165", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2069", parent != NULL);

	/*
	 * inherit missing plugins from parent
	 */

	grab_plugin_pset(object, parent, PSET_FILE);
	grab_plugin_pset(object, parent, PSET_SD);
	grab_plugin_pset(object, parent, PSET_FORMATTING);
	grab_plugin_pset(object, parent, PSET_PERM);
	return 0;
}

/* this is common implementation of adjust_to_parent method of file plugin for
   typical directories
 */
int adjust_to_parent_common_dir(struct inode *object /* new object */ ,
				struct inode *parent /* parent directory */ ,
				struct inode *root/* root directory */)
{
	int result = 0;
	pset_member memb;

	assert("nikita-2166", object != NULL);
	if (parent == NULL)
		parent = root;
	assert("nikita-2167", parent != NULL);

	/*
	 * inherit missing plugins from parent
	 */
	for (memb = 0; memb < PSET_LAST; ++memb) {
		result = grab_plugin_pset(object, parent, memb);
		if (result != 0)
			break;
	}
	return result;
}

int adjust_to_parent_cryptcompress(struct inode *object /* new object */ ,
				   struct inode *parent /* parent directory */,
				   struct inode *root/* root directory */)
{
	int result;
	result = adjust_to_parent_common(object, parent, root);
	if (result)
		return result;
	assert("edward-1416", parent != NULL);

	grab_plugin_pset(object, parent, PSET_CLUSTER);
	grab_plugin_pset(object, parent, PSET_CIPHER);
	grab_plugin_pset(object, parent, PSET_DIGEST);
	grab_plugin_pset(object, parent, PSET_COMPRESSION);
	grab_plugin_pset(object, parent, PSET_COMPRESSION_MODE);

	return 0;
}

/*
 * this is common implementation of ->create_object() of file plugins
 */
int reiser4_create_object_common(struct inode *object, struct inode *parent,
				 reiser4_object_create_data *data, oid_t *oid)
{
	assert("nikita-744", object != NULL);
	assert("nikita-745", parent != NULL);
	assert("nikita-747", data != NULL);
	assert("nikita-748", reiser4_inode_get_flag(object, REISER4_NO_SD));

	return write_sd_by_inode_common(object, oid);
}

/**
 * Reserve disk space to update stat-data item
 */
int reserve_update_sd_common(struct inode *inode)
{
	reiser4_block_nr amount;

	assert("vs-1249",
	       inode_file_plugin(inode)->estimate.update ==
	       estimate_update_common);

	amount = inode_file_plugin(inode)->estimate.update(inode);

	return reiser4_grab_space_force(amount, BA_CAN_COMMIT,
					get_meta_subvol());
}

/**
 * grab space which is needed to remove 2 items from the tree:
 * stat data and safe-link
 * @inode: object to be deleted
 */
static int reserve_delete_object(struct inode *inode)
{
	reiser4_subvol *subv = get_meta_subvol();

	return reiser4_grab_space_force(2 *
					estimate_one_item_removal(&subv->tree),
					BA_RESERVED | BA_CAN_COMMIT, subv);
}

static int common_object_delete_no_reserve(struct inode *inode);

/**
 * reiser4_delete_object_common - delete_object of file_plugin
 * @inode: inode to be deleted
 *
 * Common implementation of ->delete_object() of file_plugin.
 * It applies to object its deletion consists of removing two items - stat data
 * and safe-link.
 */
int reiser4_delete_object_common(struct inode *inode)
{
	int ret;

	assert("nikita-1477", inode != NULL);
	/*
	 * FIXME: if file body deletion failed (i/o error, for instance),
	 * inode->i_size can be != 0 here
	 */
	assert("nikita-3420", inode->i_size == 0 || S_ISLNK(inode->i_mode));
	assert("nikita-3421", inode->i_nlink == 0);

	if (reiser4_inode_get_flag(inode, REISER4_NO_SD))
		return 0;
	ret = reserve_delete_object(inode);
	if (ret)
		return ret;
	return common_object_delete_no_reserve(inode);
}

/**
 * reiser4_delete_dir_common - delete_object of file_plugin
 * @inode: inode to be deleted
 *
 * This is common implementation of delete_object method of file_plugin for
 * typical directory. It calls done method of dir_plugin to remove "." and
 * removes stat data and safe-link.
 */
int reiser4_delete_dir_common(struct inode *inode)
{
	int result;
	dir_plugin *dplug;

	assert("", (get_current_context() &&
		    get_current_context()->trans->atom == NULL));

	dplug = inode_dir_plugin(inode);
	assert("vs-1101", dplug && dplug->done);
	/*
	 * kill cursors which might be attached to inode
	 */
	reiser4_kill_cursors(inode);
	result = reserve_delete_object(inode);
	if (result)
		return result;
	result = dplug->done(inode);
	if (result)
		return result;
	return common_object_delete_no_reserve(inode);
}

/* this is common implementation of add_link method of file plugin
 */
int reiser4_add_link_common(struct inode *object, struct inode *parent)
{
	/*
	 * increment ->i_nlink and update ->i_ctime
	 */

	INODE_INC_NLINK(object);
	object->i_ctime = current_time(object);
	return 0;
}

/* this is common implementation of rem_link method of file plugin
 */
int reiser4_rem_link_common(struct inode *object, struct inode *parent)
{
	assert("nikita-2021", object != NULL);
	assert("nikita-2163", object->i_nlink > 0);

	/*
	 * decrement ->i_nlink and update ->i_ctime
	 */

	INODE_DROP_NLINK(object);
	object->i_ctime = current_time(object);
	return 0;
}

/* this is common implementation of rem_link method of file plugin for typical
   directory
*/
int rem_link_common_dir(struct inode *object, struct inode *parent UNUSED_ARG)
{
	assert("nikita-20211", object != NULL);
	assert("nikita-21631", object->i_nlink > 0);

	/*
	 * decrement ->i_nlink and update ->i_ctime
	 */
	if(object->i_nlink == 2)
		INODE_SET_NLINK(object, 0);

	else
		INODE_DROP_NLINK(object);
	object->i_ctime = current_time(object);
	return 0;
}

/* this is common implementation of owns_item method of file plugin
   compare objectids of keys in inode and coord */
int owns_item_common(const struct inode *inode,	/* object to check
						 * against */
		     const coord_t *coord/* coord to check */)
{
	reiser4_key item_key;
	reiser4_key file_key;

	assert("nikita-760", inode != NULL);
	assert("nikita-761", coord != NULL);

	return coord_is_existing_item(coord) &&
	    (get_key_objectid(build_sd_key(inode, &file_key)) ==
	     get_key_objectid(item_key_by_coord(coord, &item_key)));
}

/* this is common implementation of owns_item method of file plugin
   for typical directory
*/
int owns_item_common_dir(const struct inode *inode,/* object to check against */
			 const coord_t *coord/* coord of item to check */)
{
	reiser4_key item_key;

	assert("nikita-1335", inode != NULL);
	assert("nikita-1334", coord != NULL);

	if (plugin_of_group(item_plugin_by_coord(coord), DIR_ENTRY_ITEM_TYPE))
		return get_key_locality(item_key_by_coord(coord, &item_key)) ==
		    get_inode_oid(inode);
	else
		return owns_item_common(inode, coord);
}

/* this is common implementation of can_add_link method of file plugin
   checks whether yet another hard links to this object can be added
*/
int can_add_link_common(const struct inode *object/* object to check */)
{
	assert("nikita-732", object != NULL);

	/* inode->i_nlink is unsigned int, so just check for integer
	   overflow */
	return object->i_nlink + 1 != 0;
}

/* this is common implementation of can_rem_link method of file plugin for
   typical directory
*/
int can_rem_link_common_dir(const struct inode *inode)
{
	/* is_dir_empty() returns 0 is dir is empty */
	return !is_dir_empty(inode);
}

/* this is common implementation of detach method of file plugin for typical
   directory
*/
int reiser4_detach_common_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2883", dplug != NULL);
	assert("nikita-2884", dplug->detach != NULL);
	return dplug->detach(child, parent);
}

/* this is common implementation of bind method of file plugin for typical
   directory
*/
int reiser4_bind_common_dir(struct inode *child, struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(child);
	assert("nikita-2646", dplug != NULL);
	return dplug->attach(child, parent);
}

static int process_truncate(struct inode *, __u64 size);

/* this is common implementation of safelink method of file plugin
 */
int safelink_common(struct inode *object, reiser4_safe_link_t link, __u64 value)
{
	int result;

	assert("vs-1705", get_current_context()->trans->atom == NULL);
	if (link == SAFE_UNLINK)
		/* nothing to do. iput() in the caller (process_safelink) will
		 * finish with file */
		result = 0;
	else if (link == SAFE_TRUNCATE)
		result = process_truncate(object, value);
	else {
		warning("nikita-3438", "Unrecognized safe-link type: %i", link);
		result = RETERR(-EIO);
	}
	return result;
}

/* this is common implementation of estimate.create method of file plugin
   can be used when object creation involves insertion of one item (usually stat
   data) into tree
*/
reiser4_block_nr estimate_create_common(const struct inode *object)
{
	return estimate_one_insert_item(meta_subvol_tree());
}

/* this is common implementation of estimate.create method of file plugin for
   typical directory
   can be used when directory creation involves insertion of two items (usually
   stat data and item containing "." and "..") into tree
*/
reiser4_block_nr estimate_create_common_dir(const struct inode *object)
{
	return 2 * estimate_one_insert_item(meta_subvol_tree());
}

/* this is common implementation of estimate.update method of file plugin
   can be used when stat data update does not do more than inserting a unit
   into a stat data item which is probably true for most cases
*/
reiser4_block_nr estimate_update_common(const struct inode *inode)
{
	return estimate_one_insert_into_item(meta_subvol_tree());
}

/* this is common implementation of estimate.unlink method of file plugin
 */
reiser4_block_nr
estimate_unlink_common(const struct inode *object UNUSED_ARG,
		       const struct inode *parent UNUSED_ARG)
{
	return 0;
}

/* this is common implementation of estimate.unlink method of file plugin for
   typical directory
*/
reiser4_block_nr
estimate_unlink_common_dir(const struct inode *object,
			   const struct inode *parent)
{
	dir_plugin *dplug;

	dplug = inode_dir_plugin(object);
	assert("nikita-2888", dplug != NULL);
	assert("nikita-2887", dplug->estimate.unlink != NULL);
	return dplug->estimate.unlink(object, parent);
}

char *wire_write_common(struct inode *inode, char *start)
{
	return build_inode_onwire(inode, start);
}

char *wire_read_common(char *addr, reiser4_object_on_wire * obj)
{
	if (!obj)
		return locate_obj_key_id_onwire(addr);
	return extract_obj_key_id_from_onwire(addr, &obj->u.std.key_id);
}

struct dentry *wire_get_common(struct super_block *sb,
			       reiser4_object_on_wire * obj)
{
	struct inode *inode;
	struct dentry *dentry;
	reiser4_key key;

	extract_key_from_id(&obj->u.std.key_id, &key);
	inode = reiser4_iget(sb, &key, FIND_EXACT, 1);
	if (!IS_ERR(inode)) {
		reiser4_iget_complete(inode);
		dentry = d_obtain_alias(inode);
		if (!IS_ERR(dentry))
			dentry->d_op = &get_super_private(sb)->ops.dentry;
	} else if (PTR_ERR(inode) == -ENOENT)
		/*
		 * inode wasn't found at the key encoded in the file
		 * handle. Hence, file handle is stale.
		 */
		dentry = ERR_PTR(RETERR(-ESTALE));
	else
		dentry = (void *)inode;
	return dentry;
}

int wire_size_common(struct inode *inode)
{
	return inode_onwire_size(inode);
}

void wire_done_common(reiser4_object_on_wire * obj)
{
	/* nothing to do */
}

/* helper function to print errors */
static void key_warning(const reiser4_key * key /* key to print */ ,
			const struct inode *inode,
			int code/* error code to print */)
{
	assert("nikita-716", key != NULL);

	if (code != -ENOMEM) {
		warning("nikita-717", "Error for inode %llu (%i)",
			(unsigned long long)get_key_objectid(key), code);
		reiser4_print_key("for key", key);
	}
}

/* NIKITA-FIXME-HANS: perhaps this function belongs in another file? */
#if REISER4_DEBUG
static void
check_inode_seal(const struct inode *inode,
		 const coord_t *coord, const reiser4_key * key)
{
	reiser4_key unit_key;

	unit_key_by_coord(coord, &unit_key);
	assert("nikita-2752",
	       WITH_DATA_RET(coord->node, 1, keyeq(key, &unit_key)));
	assert("nikita-2753", get_inode_oid(inode) == get_key_objectid(key));
}

static void check_sd_coord(coord_t *coord, const reiser4_key *key)
{
	coord_clear_iplug(coord);
	if (zload(coord->node))
		return;
	if (!coord_is_existing_unit(coord) ||
	    !item_plugin_by_coord(coord) ||
	    (znode_get_level(coord->node) != LEAF_LEVEL) ||
	    !item_is_statdata(coord)) {
		warning("nikita-1901", "Conspicuous seal");
		reiser4_print_key("key", key);
		print_coord("coord", coord, 1);
		impossible("nikita-2877", "no way");
	}
	zrelse(coord->node);
}
#else
#define check_inode_seal(inode, coord, key) noop
#define check_sd_coord(coord, key) noop
#endif

/**
 * insert new stat-data into tree. Called with inode state
 * locked. Return inode state locked.
 * @inode - inode to create stat-data for;
 * @oid - pre-allocated object id.
 */
static int insert_new_sd(struct inode *inode, oid_t oid)
{
	int result;
	reiser4_key key;
	coord_t coord;
	reiser4_item_data data;
	char *area;
	reiser4_inode *ref;
	lock_handle lh;

	assert("nikita-723", inode != NULL);
	assert("nikita-3406", reiser4_inode_get_flag(inode, REISER4_NO_SD));

	ref = reiser4_inode_data(inode);
	spin_lock_inode(inode);

	if (ref->plugin_mask != 0)
		/* inode has non-standard plugins */
		inode_set_extension(inode, PLUGIN_STAT);
	/*
	 * prepare specification of new item to be inserted
	 */

	data.iplug = inode_sd_plugin(inode);
	data.length = data.iplug->s.sd.save_len(inode);
	spin_unlock_inode(inode);

	data.data = NULL;
	data.user = 0;
	/*
	 * could be optimized for case where there is only one node
	 * format in use in the filesystem, probably there are lots
	 * of such places we could optimize for only one node layout.
	 * -Hans
	 */
	if (data.length > meta_subvol_tree()->nplug->max_item_size()) {
		/*
		 * This is silly check, but we don't know actual node
		 * where insertion will go into
		 */
		return RETERR(-ENAMETOOLONG);
	}
	/*
	 * oid = oid_allocate(inode->i_sb);
	 * NIKITA-FIXME-HANS: what is your opinion on whether this error
	 * check should be encapsulated into oid_allocate?
	 * if (oid == ABSOLUTE_MAX_OID)
	 *        return RETERR(-EOVERFLOW);
	 *
	 * oid had been allocated before grabbing space for the
	 * new stat-data as we need to know id of the subvolume
	 * where this stat-data will be written to. - Edward.
	 */
	set_inode_oid(inode, oid);

	coord_init_zero(&coord);
	init_lh(&lh);

	result = insert_by_key(meta_subvol_tree(),
			       build_sd_key(inode, &key), &data, &coord, &lh,
			       /* stat data lives on a leaf level */
			       LEAF_LEVEL, CBK_UNIQUE);

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
				reiser4_inode_clr_flag(inode, REISER4_NO_SD);
				reiser4_inode_set_flag(inode,
						       REISER4_SDLEN_KNOWN);
				/* initialise stat-data seal */
				reiser4_seal_init(&ref->sd_seal, &coord, &key);
				ref->sd_coord = coord;
				check_inode_seal(inode, &coord, &key);
			} else if (result != -ENOMEM)
				/*
				 * convert any other error code to -EIO to
				 * avoid confusing user level with unexpected
				 * errors.
				 */
				result = RETERR(-EIO);
			zrelse(coord.node);
		}
	}
	done_lh(&lh);

	if (result != 0)
		key_warning(&key, inode, result);
	else
		oid_count_allocated();

	return result;
}

/**
 * Find stat-data in a tree by key.
 *
 * Sometimes we are not able to construct precise key to look for
 * stat-data (specifically, its ordering component is unknown).
 * In this case we set maximal possible ordering value and perform
 * lookup with FIND_MAX_NOT_MORE_THAN lookup bias.
 *
 *@inode: inode to look stat-data for
 *@key: key of stat-data
 *@bias: lookup bias -
 *       FIND_EXACT, if the key is precise,
 *       FIND_MAX_NOT_MORE_THAN, if we don't know ordering component
 *       of the key
 *@coord: resulting coord
 *@lh: resulting lock handle
 */
int lookup_sd(struct inode *inode, znode_lock_mode lock_mode,
	      coord_t *coord, lock_handle *lh, const reiser4_key *key,
	      lookup_bias bias, int silent)
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
	/*
	 * traverse tree to find stat data. We cannot use vroot here, because
	 * it only covers _body_ of the file, and stat data don't belong
	 * there.
	 */
	result = coord_by_key(meta_subvol_tree(),
			      key,
			      coord,
			      lh,
			      lock_mode,
			      bias, LEAF_LEVEL, LEAF_LEVEL, flags, NULL);
	if (unlikely(IS_CBKERR(result))) {
		key_warning(key, inode, result);
		return result;
	}
	if (result == CBK_COORD_FOUND) {
		check_sd_coord(coord, key);
		return 0;
	}
	/* not found */
	if (bias == FIND_MAX_NOT_MORE_THAN) {
		/*
		 * In this mode we don't expect that stat-data,
		 * we are looking for, necessarily exists.
		 */
		if (coord->between != AFTER_ITEM) {
			warning("edward-2320",
				"Unexpected between state (%d)",
				coord->between);
			key_warning(key, inode, result);
			return -EIO;
		}
		coord->between = AT_UNIT;
		coord->unit_pos = 0;
		return 0;
	}
	/* not found by exact key */
	if (!silent)
		key_warning(key, inode, result);
	return result;
}

static int locate_inode_sd(struct inode *inode,
			   reiser4_key *key, coord_t *coord, lock_handle *lh)
{
	reiser4_inode *state;
	seal_t seal;
	int result;

	assert("nikita-3483", inode != NULL);

	state = reiser4_inode_data(inode);
	spin_lock_inode(inode);
	*coord = state->sd_coord;
	coord_clear_iplug(coord);
	seal = state->sd_seal;
	spin_unlock_inode(inode);

	build_sd_key(inode, key);
	/* first, try to use seal */
	if (reiser4_seal_is_set(&seal)) {
		result = reiser4_seal_validate(&seal,
					       meta_subvol_tree(),
					       coord,
					       key,
					       lh, ZNODE_WRITE_LOCK,
					       ZNODE_LOCK_LOPRI);
		if (result == 0) {
			check_sd_coord(coord, key);
			return 0;
		}
	}
	/* hint is invalid,
	 * so traverse tree
	 */
	coord_init_zero(coord);
	return lookup_sd(inode, ZNODE_WRITE_LOCK, coord, lh, key,
			 FIND_EXACT, 0);
}

#if REISER4_DEBUG
static int all_but_offset_key_eq(const reiser4_key * k1, const reiser4_key * k2)
{
	return (get_key_locality(k1) == get_key_locality(k2) &&
		get_key_type(k1) == get_key_type(k2) &&
		get_key_band(k1) == get_key_band(k2) &&
		get_key_ordering(k1) == get_key_ordering(k2) &&
		get_key_objectid(k1) == get_key_objectid(k2));
}

#include "../tree_walk.h"

/* make some checks before and after stat-data resize operation */
static int check_sd_resize(struct inode *inode, coord_t *coord,
			   int length, int progress/* 1 means after resize */)
{
	int ret = 0;
	lock_handle left_lock;
	coord_t left_coord;
	reiser4_key left_key;
	reiser4_key key;

	if (inode_file_plugin(inode) !=
	    file_plugin_by_id(CRYPTCOMPRESS_FILE_PLUGIN_ID))
		return 0;
	if (!length)
		return 0;
	if (coord->item_pos != 0)
		return 0;

	init_lh(&left_lock);
	ret = reiser4_get_left_neighbor(&left_lock,
					coord->node,
					ZNODE_WRITE_LOCK,
					GN_CAN_USE_UPPER_LEVELS);
	if (ret == -E_REPEAT || ret == -E_NO_NEIGHBOR ||
	    ret == -ENOENT || ret == -EINVAL
	    || ret == -E_DEADLOCK) {
		ret = 0;
		goto exit;
	}
	ret = zload(left_lock.node);
	if (ret)
		goto exit;
	coord_init_last_unit(&left_coord, left_lock.node);
	item_key_by_coord(&left_coord, &left_key);
	item_key_by_coord(coord, &key);

	if (all_but_offset_key_eq(&key, &left_key))
		/* corruption occured */
		ret = 1;
	zrelse(left_lock.node);
 exit:
	done_lh(&left_lock);
	return ret;
}
#endif

/* update stat-data at @coord */
static int
update_sd_at(struct inode *inode, coord_t *coord, reiser4_key * key,
	     lock_handle * lh)
{
	int result;
	reiser4_item_data data;
	char *area;
	reiser4_inode *state;
	znode *loaded;

	state = reiser4_inode_data(inode);

	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (result != 0)
		return result;
	loaded = coord->node;

	spin_lock_inode(inode);
	assert("nikita-728", inode_sd_plugin(inode) != NULL);
	data.iplug = inode_sd_plugin(inode);

	/* if inode has non-standard plugins, add appropriate stat data
	 * extension */
	if (state->extmask & (1 << PLUGIN_STAT)) {
		if (state->plugin_mask == 0)
			inode_clr_extension(inode, PLUGIN_STAT);
	} else if (state->plugin_mask != 0)
		inode_set_extension(inode, PLUGIN_STAT);

	if (state->extmask & (1 << HEIR_STAT)) {
		if (state->heir_mask == 0)
			inode_clr_extension(inode, HEIR_STAT);
	} else if (state->heir_mask != 0)
			inode_set_extension(inode, HEIR_STAT);

	/* data.length is how much space to add to (or remove
	   from if negative) sd */
	if (!reiser4_inode_get_flag(inode, REISER4_SDLEN_KNOWN)) {
		/* recalculate stat-data length */
		data.length =
		    data.iplug->s.sd.save_len(inode) -
		    item_length_by_coord(coord);
		reiser4_inode_set_flag(inode, REISER4_SDLEN_KNOWN);
	} else
		data.length = 0;
	spin_unlock_inode(inode);

	/* if on-disk stat data is of different length than required
	   for this inode, resize it */

	if (data.length != 0) {
		data.data = NULL;
		data.user = 0;

		assert("edward-1441",
		       !check_sd_resize(inode, coord,
					data.length, 0/* before resize */));

		/* insertion code requires that insertion point (coord) was
		 * between units. */
		coord->between = AFTER_UNIT;
		result = reiser4_resize_item(coord, &data, key, lh,
					     COPI_DONT_SHIFT_LEFT);
		if (result != 0) {
			key_warning(key, inode, result);
			zrelse(loaded);
			return result;
		}
		if (loaded != coord->node) {
		  /* reiser4_resize_item moved coord to another node.
		     Zload it */
			zrelse(loaded);
			coord_clear_iplug(coord);
			result = zload(coord->node);
			if (result != 0)
				return result;
			loaded = coord->node;
		}
		assert("edward-1442",
		       !check_sd_resize(inode, coord,
					data.length, 1/* after resize */));
	}
	area = item_body_by_coord(coord);
	spin_lock_inode(inode);
	result = data.iplug->s.sd.save(inode, &area);
	znode_make_dirty(coord->node);

	/* re-initialise stat-data seal */

	/*
	 * coord.between was possibly skewed from AT_UNIT when stat-data size
	 * was changed and new extensions were pasted into item.
	 */
	coord->between = AT_UNIT;
	reiser4_seal_init(&state->sd_seal, coord, key);
	state->sd_coord = *coord;
	spin_unlock_inode(inode);
	check_inode_seal(inode, coord, key);
	zrelse(loaded);
	return result;
}

/* Update existing stat-data in a tree. Called with inode state locked. Return
   inode state locked. */
static int update_sd(struct inode *inode/* inode to update sd for */)
{
	int result;
	reiser4_key key;
	coord_t coord;
	lock_handle lh;

	assert("nikita-726", inode != NULL);

	/* no stat-data, nothing to update?! */
	assert("nikita-3482", !reiser4_inode_get_flag(inode, REISER4_NO_SD));

	init_lh(&lh);

	result = locate_inode_sd(inode, &key, &coord, &lh);
	if (result == 0)
		result = update_sd_at(inode, &coord, &key, &lh);
	done_lh(&lh);

	return result;
}

/**
 * Helper for reiser4_delete_object_common and reiser4_delete_dir_common.
 * Remove object's body, stat data and safe link from the tree.
 * Space for that must be reserved by caller before
 * @inode: object to be deleted
 */
static int common_object_delete_no_reserve(struct inode *inode)
{
	int result;

	assert("nikita-1477", inode != NULL);

	if (!reiser4_inode_get_flag(inode, REISER4_NO_SD)) {
		reiser4_key sd_key;

		build_sd_key(inode, &sd_key);
		result = reiser4_cut_tree(meta_subvol_tree(),
					  &sd_key, &sd_key, NULL, 0);
		if (result == 0) {
			reiser4_inode_set_flag(inode, REISER4_NO_SD);
			result = oid_release(inode->i_sb, get_inode_oid(inode));
			if (result == 0) {
				oid_count_released();

				result = safe_link_del(get_meta_subvol(),
						       get_inode_oid(inode),
						       SAFE_UNLINK);
			}
		}
	} else
		result = 0;
	return result;
}

/* helper for safelink_common */
static int process_truncate(struct inode *inode, __u64 size)
{
	int result;
	struct iattr attr;
	file_plugin *fplug;
	reiser4_context *ctx;
	struct dentry dentry;

	assert("vs-21", is_in_reiser4_context());
	ctx = reiser4_init_context(inode->i_sb);
	assert("vs-22", !IS_ERR(ctx));

	attr.ia_size = size;
	attr.ia_valid = ATTR_SIZE | ATTR_CTIME;
	fplug = inode_file_plugin(inode);

	inode_lock(inode);
	assert("vs-1704", get_current_context()->trans->atom == NULL);
	dentry.d_inode = inode;
	result = inode->i_op->setattr(&init_user_ns, &dentry, &attr);
	inode_unlock(inode);

	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);

	return result;
}

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
