/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Methods of directory plugin.
 */

/* version 3 has no directory read-ahead.  This is silly/wrong.  It
   would be nice if there was some commonality between file and
   directory read-ahead code, but I am not sure how well this can be
   done.  */

#include "../../forward.h"
#include "../../debug.h"
#include "../../spin_macros.h"
#include "../plugin_header.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../tslist.h"
#include "../plugin.h"
#include "dir.h"
#include "../item/item.h"
#include "../security/perm.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tap.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../super.h"
#include "../object.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct file  */
#include <linux/quotaops.h>
#include <linux/dcache.h>	/* for struct dentry */

/*
 * Directory read-ahead control.
 *
 * FIXME-NIKITA this is just stub. This function is supposed to be
 * called during lookup, readdir, and may be creation.
 *
 */
void
directory_readahead(struct inode *dir /* directory being accessed */ ,
		    coord_t * coord /* coord of access */ )
{
	assert("nikita-1682", dir != NULL);
	assert("nikita-1683", coord != NULL);
	assert("nikita-1684", coord->node != NULL);
	assert("nikita-1685", znode_is_any_locked(coord->node));

	trace_stamp(TRACE_DIR);
}

/** 
 * helper function. Standards require than for many file-system operations
 * on success ctime and mtime of parent directory is to be updated.
 */
static int
update_dir(struct inode *dir)
{
	assert("nikita-2525", dir != NULL);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	return reiser4_write_sd(dir);
}

static reiser4_block_nr common_estimate_link(
	struct inode *parent /* parent directory */,
	struct inode *object /* object to which new link is being cerated */) 
{
	reiser4_block_nr res = 0;
	file_plugin *fplug; 
	dir_plugin *dplug;

	assert("vpf-317", object != NULL);
	assert("vpf-318", parent != NULL );

	fplug = inode_file_plugin(object);
	dplug = inode_dir_plugin(parent);
	
	/* reiser4_add_nlink(object) */
	res += fplug->estimate.update(object);
	/* add_entry(parent) */
	res += dplug->estimate.add_entry(parent);
	/* reiser4_del_nlink(object) */
	res += fplug->estimate.update(object);
	/* update_dir(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);

	return res;
}

/** 
 * add link from @parent directory to @existing object.
 *
 *     . get plugins
 *     . check permissions
 *     . check that "existing" can hold yet another link
 *     . start transaction
 *     . add link to "existing"
 *     . add entry to "parent"
 *     . if last step fails, remove link from "existing"
 *
 */
static int
common_link(struct inode *parent /* parent directory */ ,
	    struct dentry *existing	/* dentry of object to which
					 * new link is being
					 * cerated */ ,
	    struct dentry *where /* new name */ )
{
	int result;
	struct inode *object;
	file_plugin *fplug;
	dir_plugin *parent_dplug;
	reiser4_dir_entry_desc entry;
	reiser4_object_create_data data;
	reiser4_block_nr reserve;

	assert("nikita-1431", existing != NULL);
	assert("nikita-1432", parent != NULL);
	assert("nikita-1433", where != NULL);

	object = existing->d_inode;
	assert("nikita-1434", object != NULL);

	fplug = inode_file_plugin(object);

	/* check for race with create_object() */
	if (inode_get_flag(object, REISER4_IMMUTABLE))
		return -EAGAIN;

	/* links to directories are not allowed if file-system
	   logical name-space should be ADG */
	if (reiser4_is_set(parent->i_sb, REISER4_ADG) &&
	    S_ISDIR(object->i_mode)) return -EISDIR;

	/* check permissions */
	if (perm_chk(parent, link, existing, parent, where))
		return -EPERM;

	parent_dplug = inode_dir_plugin(parent);

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	data.mode = object->i_mode;
	data.id = inode_file_plugin(object)->h.id;
	if ((reserve = common_estimate_link(parent, existing->d_inode)) < 0)
	    return reserve;

	warning("vpf-323", "SPACE: link grabs %llu blocks.", reserve);	
	if (reiser4_grab_space_exact(reserve, 0))
	    return -ENOSPC;

	result = reiser4_add_nlink(object, parent, 1);
	if (result == 0) {
		/* add entry to the parent */
		result = parent_dplug->add_entry(parent, where, &data, &entry);
		if (result != 0) {
			/* failure to add entry to the parent, remove
			   link from "existing" */
			result = reiser4_del_nlink(object, parent, 1);
			/* now, if this fails, we have a file with too
			   big nlink---space leak, much better than
			   directory entry pointing to nowhere */
			/* may be it should be recorded somewhere, but
			   if addition of link to parent and update of
			   object's stat data both failed, chances are
			   that something is going really wrong */
		}
	}
	if (result == 0) {
		atomic_inc(&object->i_count);
		/*
		 * Upon successful completion, link() shall mark for update
		 * the st_ctime field of the file. Also, the st_ctime and
		 * st_mtime fields of the directory that contains the new
		 * entry shall be marked for update. --SUS
		 */
		result = update_dir(parent);
	}
	return result;
}

static reiser4_block_nr common_estimate_unlink (
	struct inode *parent /* parent directory */,
	struct inode *object /* object to which new link is being cerated */) 
{
	reiser4_block_nr res = 0;
	file_plugin *fplug; 
	dir_plugin *dplug;
	
	assert("vpf-317", object != NULL);
	assert("vpf-318", parent != NULL );

	fplug = inode_file_plugin(object);
	dplug = inode_dir_plugin(parent);
	
	/* rem_entry(parent) */
	res += dplug->estimate.rem_entry(parent);
	/* reiser4_del_nlink(object) */
	res += fplug->estimate.update(object);
	/* update_dir(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);

	return res;
}

/** 
 * remove link from @parent directory to @victim object.
 *
 *     . get plugins
 *     . find entry in @parent
 *     . check permissions
 *     . decrement nlink on @victim
 *     . if nlink drops to 0, delete object
 */
static int
common_unlink(struct inode *parent /* parent object */ ,
	      struct dentry *victim	/* name being removed from
					 * @parent */ )
{
	int result;
	struct inode *object;
	file_plugin *fplug;
	dir_plugin *parent_dplug;
	reiser4_dir_entry_desc entry;
	reiser4_block_nr reserve;

	assert("nikita-864", parent != NULL);
	assert("nikita-865", victim != NULL);

	object = victim->d_inode;
	assert("nikita-1239", object != NULL);

	fplug = inode_file_plugin(object);
	if ((reserve = common_estimate_unlink(parent, victim->d_inode)) < 0)
		return reserve;

	if (reiser4_grab_space_exact(reserve, 1))
		return -ENOSPC;
	
	warning("vpf-324", "SPACE: unlink grabs %llu blocks.", reserve);	
	/* check for race with create_object() */
	if (inode_get_flag(object, REISER4_IMMUTABLE))
		return -EAGAIN;

	/* victim should have stat data */
	assert("vs-949", !inode_get_flag(object, REISER4_NO_SD));

	/* check permissions */
	if (perm_chk(parent, unlink, parent, victim))
		return -EPERM;

	/* ask object plugin */
	if (fplug->can_rem_link != NULL) {
		result = fplug->can_rem_link(object);
		if (result != 0)
			return result;
	}

	parent_dplug = inode_dir_plugin(parent);

	xmemset(&entry, 0, sizeof entry);

	/* first, delete directory entry */
	result = parent_dplug->rem_entry(parent, victim, &entry);
	if (result != 0)
		return result;

	/*
	 * now that directory entry is removed, update stat-data, but first
	 * check for special case:
	 */
	if (fplug->rem_link != 0)
		result = reiser4_del_nlink(object, parent, 1);
	else
		result = -EPERM;
	if (result != 0)
		return result;

#if 0
	/* 
	 * removing last reference. Check that this is allowed. This is
	 * optimization for common case when file having only one name is
	 * unlinked and is not opened by any process.
	 *
	 * Directories always go through this path (until hard-links on
	 * directories are allowed).
	 */
	/*
	 * FIXME-NIKITA this is commented out, because ->i_count check is not
	 * valid---actually we also should check victim->d_count, but this
	 * requires spin_lock(&dcache_lock), so we need cut-n-paste something
	 * from d_delete().
	 */
	inode_set_flag(object, REISER4_IMMUTABLE);
	if (fplug->not_linked(object) &&
	    atomic_read(&object->i_count) == 1 &&
	    !perm_chk(object, delete, parent, victim)) {
		/* 
		 * remove file body. This is probably done in a whole lot of
		 * transactions and takes a lot of time. We keep @object
		 * locked. i_nlink shouldn't change, because object is
		 * inaccessible through file-system (last directory entry was
		 * removed), and direct accessors (like NFS) are blocked by
		 * REISER4_IMMUTABLE bit. 
		 */
		if (fplug->truncate != NULL)
			result = truncate_object(object, (loff_t) 0);

		assert("nikita-871", fplug->not_linked(object));
		assert("nikita-873", atomic_read(&object->i_count) == 1);

		if (result == 0)
			result = fplug->delete(object, parent);
	}
	inode_clr_flag(object, REISER4_IMMUTABLE);
#endif
	/*
	 * Upon successful completion, unlink() shall mark for update the
	 * st_ctime and st_mtime fields of the parent directory. Also, if the
	 * file's link count is not 0, the st_ctime field of the file shall be
	 * marked for update. --SUS
	 */
	if (result == 0)
		result = update_dir(parent);
	/*
	 * @object's i_ctime was updated by ->rem_link() method().
	 */
	return result;
}

/* Estimate the maximum amount of nodes will be allocated or changed for:
 * - insert an in the parent entry
 * - update the SD of parent
 * - estimate child creation
 */
static reiser4_block_nr common_estimate_create_dir( 
	struct inode *parent, /* parent object */
	struct inode *object /* object */)
{
	assert("vpf-309", parent != NULL);
	assert("vpf-307", object != NULL);
	
	return (inode_file_plugin(object)->estimate.create(tree_by_inode(parent)->height, object) +
	       	(inode_dir_plugin(object) ? 
		    inode_dir_plugin(object)->estimate.init(parent, object) : 0) + 
	       	inode_file_plugin(parent)->estimate.update(parent) + 
		/* FIXME: call oid-alloc estimate */
		inode_dir_plugin(parent)->estimate.add_entry(parent) + 
		inode_dir_plugin(parent)->estimate.rem_entry(parent));
}

/**
 * Create child in directory.
 *
 * . get object's plugin
 * . get fresh inode
 * . initialize inode
 * . add object's stat-data
 * . initialize object's directory
 * . add entry to the parent
 * . instantiate dentry
 *
 */
static int
common_create_child(struct inode *parent /* parent object */ ,
		    struct dentry *dentry /* new name */ ,
		    reiser4_object_create_data * data	/* parameters
							 * of new
							 * object */ )
{
	int result;

	dir_plugin *par_dir;	/* directory plugin on the parent */
	dir_plugin *obj_dir;	/* directory plugin on the new object */
	file_plugin *obj_plug;	/* object plugin on the new object */
	struct inode *object;	/* new object */
	reiser4_block_nr reserve;

	reiser4_dir_entry_desc entry;	/* new directory entry */

	assert("nikita-1418", parent != NULL);
	assert("nikita-1419", dentry != NULL);
	assert("nikita-1420", data != NULL);
	par_dir = inode_dir_plugin(parent);
	/* check permissions */
	if (perm_chk(parent, create, parent, dentry, data)) {
		return -EPERM;
	}

	/* check, that name is acceptable for parent */
	if (par_dir->is_name_acceptable &&
	    !par_dir->is_name_acceptable(parent, dentry->d_name.name,
					 (int) dentry->d_name.len)) {
		return -ENAMETOOLONG;
	}

	result = 0;
	obj_plug = file_plugin_by_id((int) data->id);
	if (obj_plug == NULL) {
		warning("nikita-430", "Cannot find plugin %i", data->id);
		return -ENOENT;
	}
	object = new_inode(parent->i_sb);
	if (object == NULL)
		return -ENOMEM;
	/*
	 * we'll update i_nlink below
	 */
	object->i_nlink = 0;

	dentry->d_inode = object;	/* So that on error iput will be called. */

	if (DQUOT_ALLOC_INODE(object)) {
		DQUOT_DROP(object);
		object->i_flags |= S_NOQUOTA;
		return -EDQUOT;
	}

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	reiser4_inode_data(object)->file = obj_plug;
	result = obj_plug->set_plug_in_inode(object, parent, data);
	if (result) {
		warning("nikita-431", "Cannot install plugin %i on %llx",
			data->id, get_inode_oid(object));
		return result;
	}

	/* reget plugin after installation */
	obj_plug = inode_file_plugin(object);

	if (obj_plug->create == NULL)
		return -EPERM;

	/*
	 * if any of hash, tail, sd or permission plugins for newly created
	 * object are not set yet set them here inheriting them from parent
	 * directory
	 */
	assert("nikita-2070", obj_plug->adjust_to_parent != NULL);
	result = obj_plug->adjust_to_parent
	    (object, parent, object->i_sb->s_root->d_inode);
	if (result != 0) {
		warning("nikita-432", "Cannot inherit from %llx to %llx",
			get_inode_oid(parent), get_inode_oid(object));
		return result;
	}

	/* obtain directory plugin (if any) for new object. */
	obj_dir = inode_dir_plugin(object);
	if ((obj_dir != NULL) && (obj_dir->init == NULL))
		return -EPERM;

	reiser4_inode_data(object)->locality_id = get_inode_oid(parent);
	if ((reserve = common_estimate_create_dir(parent, object)) < 0)
	    return reserve;

	warning("vpf-325", "SPACE: create child grabs %llu blocks.", reserve);
	if (reiser4_grab_space_exact(reserve, 0))
	    return -ENOSPC;
	
	/*
	 * mark inode `immutable'. We disable changes to the file being
	 * created until valid directory entry for it is inserted. Otherwise,
	 * if file were expanded and insertion of directory entry fails, we
	 * have to remove file, but we only alloted enough space in
	 * transaction to remove _empty_ file. 3.x code used to remove stat
	 * data in different transaction thus possibly leaking disk space on
	 * crash. This all only matters if it's possible to access file
	 * without name, for example, by inode number
	 */
	inode_set_flag(object, REISER4_IMMUTABLE);

	/* 
	 * create empty object, this includes allocation of new objectid. For
	 * directories this implies creation of dot and dotdot 
	 */
	assert("nikita-2265", inode_get_flag(object, REISER4_NO_SD));

	/*
	 * mark inode as `loaded'. From this point onward
	 * reiser4_delete_inode() will try to remove its stat-data.
	 */
	inode_set_flag(object, REISER4_LOADED);

	result = obj_plug->create(object, parent, data);
	if (result != 0) {
		inode_clr_flag(object, REISER4_IMMUTABLE);
		if (result != -ENAMETOOLONG)
			warning("nikita-2219",
				"Failed to create sd for %llu (%lx)",
				get_inode_oid(object),
				reiser4_inode_data(object)->flags);
		return result;
	}

	if (obj_dir != NULL)
		result = obj_dir->init(object, parent, data);
	if (result == 0) {
		assert("nikita-434", !inode_get_flag(object, REISER4_NO_SD));
		/* insert inode into VFS hash table */
		insert_inode_hash(object);
		/* create entry */
		result = par_dir->add_entry(parent, dentry, data, &entry);
		if (result == 0) {
			result = reiser4_add_nlink(object, parent, 1);
			/*
			 * If O_CREAT is set and the file did not previously
			 * exist, upon successful completion, open() shall
			 * mark for update the st_atime, st_ctime, and
			 * st_mtime fields of the file and the st_ctime and
			 * st_mtime fields of the parent directory. --SUS
			 */
			/*
			 * @object times are already updated by
			 * reiser4_add_nlink()
			 */
			if (result == 0)
				result = update_dir(parent);
			if (result != 0) {
				/* cleanup failure to update times */
				dentry->d_inode = object;
				par_dir->rem_entry(parent, dentry, &entry);
				dentry->d_inode = NULL;
			}
		}
		if (result != 0)
			/* cleanup failure to add entry */
			if (obj_dir != NULL)
				obj_dir->done(object);
	} else
		warning("nikita-2219", "Failed to initialize dir for %llu: %i",
			get_inode_oid(object), result);

	if (result != 0)
		/*
		 * failure to create entry, remove object
		 */
		obj_plug->delete(object);

	/* file has name now, clear immutable flag */
	inode_clr_flag(object, REISER4_IMMUTABLE);

	/* 
	 * on error, iput() will call ->delete_inode(). We should keep track
	 * of the existence of stat-data for this inode and avoid attempt to
	 * remove it in reiser4_delete_inode(). This is accomplished through
	 * REISER4_NO_SD bit in inode.u.reiser4_i.plugin.flags
	 */
	return result;
}

/*
static common_esimate_create( 
    struct inode *parent // parent object , 
			      struct dentry *dentry // new name , 
			      reiser4_object_create_data *data // parameters
							//	* of new
							//	* object ) 
{
	assert( "vpf-307", parent != NULL );
	assert( "vpf-308", dentry != NULL );
	assert( "vpf-309", data   != NULL );
}
*/
/** ->is_name_acceptable() method of directory plugin */
/* Audited by: green(2002.06.15) */
int
is_name_acceptable(const struct inode *inode /* directory to check */ ,
		   const char *name UNUSED_ARG /* name to check */ ,
		   int len /* @name's length */ )
{
	assert("nikita-733", inode != NULL);
	assert("nikita-734", name != NULL);
	assert("nikita-735", len > 0);

	return len <= reiser4_max_filename_len(inode);
}

/** actor function looking for any entry different from dot or dotdot. */
static int
is_empty_actor(reiser4_tree * tree UNUSED_ARG /* tree scanned */ ,
	       coord_t * coord /* current coord */ ,
	       lock_handle * lh UNUSED_ARG	/* current lock
						 * handle */ ,
	       void *arg /* readdir arguments */ )
{
	struct inode *dir;
	file_plugin *fplug;
	item_plugin *iplug;
	char *name;

	assert("nikita-2004", tree != NULL);
	assert("nikita-2005", coord != NULL);
	assert("nikita-2006", arg != NULL);

	dir = arg;
	assert("nikita-2003", dir != NULL);

	if (item_id_by_coord(coord) !=
	    item_id_by_plugin(inode_dir_item_plugin(dir))) return 0;

	fplug = inode_file_plugin(dir);
	if (!fplug->owns_item(dir, coord))
		return 0;

	iplug = item_plugin_by_coord(coord);
	name = iplug->s.dir.extract_name(coord);
	assert("nikita-2162", name != NULL);

	if ((name[0] != '.') || ((name[1] != '.') && (name[1] != '\0')))
		return -ENOTEMPTY;
	else
		return 1;
}

/** true if directory is empty (only contains dot and dotdot) */
int
is_dir_empty(const struct inode *dir)
{
	reiser4_key de_key;
	int result;
	struct qstr dot;
	coord_t coord;
	lock_handle lh;

	assert("nikita-1976", dir != NULL);

	/*
	 * rely on our method to maintain directory i_size being equal to the
	 * number of entries.
	 */
	return dir->i_size <= 2 ? 0 : -ENOTEMPTY;

	/*
	 * FIXME-NIKITA this is not correct if hard links on directories are
	 * supported in this fs (if REISER4_ADG is not set in dir ->
	 * i_sb). But then, how to determine that last "outer" link is
	 * removed?
	 */

	dot.name = ".";
	dot.len = 1;

	result = inode_dir_plugin(dir)->entry_key(dir, &dot, &de_key);
	if (result != 0)
		return result;

	coord_init_zero(&coord);
	init_lh(&lh);

	result = coord_by_key(tree_by_inode(dir), &de_key, &coord, &lh,
			      ZNODE_READ_LOCK, FIND_MAX_NOT_MORE_THAN,
			      LEAF_LEVEL, LEAF_LEVEL, 0);
	switch (result) {
	case CBK_COORD_FOUND:
		result = iterate_tree(tree_by_inode(dir), &coord, &lh,
				      is_empty_actor, (void *) dir,
				      ZNODE_READ_LOCK, 1);
		switch (result) {
		default:
		case -ENOTEMPTY:
			break;
		case 0:
		case -ENAVAIL:
			result = 0;
			break;
		}
		break;
	case CBK_COORD_NOTFOUND:
		/* no entries?! */
		warning("nikita-2002", "Directory %lli is TOO empty",
			get_inode_oid(dir));
		result = 0;
		break;
	default:
		/* some other error */
		break;
	}
	done_lh(&lh);

	return result;
}

/** compare two logical positions within the same directory */
cmp_t
dir_pos_cmp(const dir_pos * p1, const dir_pos * p2)
{
	cmp_t result;

	assert("nikita-2534", p1 != NULL);
	assert("nikita-2535", p2 != NULL);

	result = de_id_cmp(&p1->dir_entry_key, &p2->dir_entry_key);
	if (result == EQUAL_TO) {
		int diff;

		diff = p1->pos - p2->pos;
		result =
		    (diff < 0) ? LESS_THAN : (diff ? GREATER_THAN : EQUAL_TO);
	}
	return result;
}

void
adjust_dir_pos(struct file *dir, readdir_pos * readdir_spot,
	       const dir_pos * mod_point, int adj)
{
	dir_pos *pos;

	reiser4_stat_dir_add(readdir.adjust_pos);
	pos = &readdir_spot->position;
	switch (dir_pos_cmp(mod_point, pos)) {
	case LESS_THAN:
		readdir_spot->entry_no += adj;
		lock_kernel();
		assert("nikita-2577", dir->f_pos + adj >= 0);
		dir->f_pos += adj;
		unlock_kernel();
		if (de_id_cmp(&pos->dir_entry_key,
			      &mod_point->dir_entry_key) == EQUAL_TO) {
			assert("nikita-2575", mod_point->pos < pos->pos);
			pos->pos += adj;
		}
		reiser4_stat_dir_add(readdir.adjust_lt);
		break;
	case GREATER_THAN:
		/*
		 * directory is modified after @pos: nothing to do.
		 */
		reiser4_stat_dir_add(readdir.adjust_gt);
		break;
	case EQUAL_TO:
		/*
		 * cannot insert an entry readdir is looking at, because it
		 * already exists.
		 */
		assert("nikita-2576", adj < 0);
		/*
		 * directory entry to which @pos points to is being
		 * removed. 
		 *
		 * FIXME-NIKITA: Right thing to do is to update @pos to point
		 * to the next entry. This is complex (we are under spin-lock
		 * for one thing). Just rewind it to the beginning. Next
		 * readdir will have to scan the beginning of
		 * directory. Proper solution is to use semaphore in
		 * spin lock's stead and use rewind_right() here.
		 */
		xmemset(readdir_spot, 0, sizeof *readdir_spot);
		reiser4_stat_dir_add(readdir.adjust_eq);
	}
}

/**
 * scan all file-descriptors for this directory and adjust their positions
 * respectively.
 */
void
adjust_dir_file(struct inode *dir, const coord_t * coord, int offset, int adj)
{
	reiser4_file_fsdata *scan;
	reiser4_inode *info;
	reiser4_key de_key;
	dir_pos mod_point;

	assert("nikita-2536", dir != NULL);
	assert("nikita-2538", coord != NULL);
	assert("nikita-2539", adj != 0);

	WITH_DATA(coord->node, unit_key_by_coord(coord, &de_key));
	build_de_id_by_key(&de_key, &mod_point.dir_entry_key);
	mod_point.pos = offset;

	info = reiser4_inode_data(dir);
	spin_lock(&info->guard);
	for (scan = readdir_list_front(&info->readdir_list);
	     !readdir_list_end(&info->readdir_list, scan);
	     scan = readdir_list_next(scan)) {
		adjust_dir_pos(scan->back, &scan->dir.readdir, &mod_point, adj);
	}
	spin_unlock(&info->guard);
}

static int
dir_go_to(struct file *dir, readdir_pos * pos, tap_t * tap)
{
	reiser4_key key;
	int result;
	struct inode *inode;

	assert("nikita-2554", pos != NULL);

	inode = dir->f_dentry->d_inode;
	result = inode_dir_plugin(inode)->readdir_key(dir, &key);
	if (result != 0)
		return result;
	result = coord_by_key(tree_by_inode(inode), &key,
			      tap->coord, tap->lh, tap->mode,
			      FIND_MAX_NOT_MORE_THAN,
			      LEAF_LEVEL, LEAF_LEVEL, 0);
	if (result == CBK_COORD_FOUND)
		result = rewind_right(tap, (int) pos->position.pos);
	else
		tap->coord->node = NULL;
	return result;
}

static int
dir_rewind(struct file *dir, readdir_pos * pos, loff_t offset, tap_t * tap)
{
	__u64 destination;
	int shift;
	int result;

	assert("nikita-2553", dir != NULL);
	assert("nikita-2548", pos != NULL);
	assert("nikita-2551", tap->coord != NULL);
	assert("nikita-2552", tap->lh != NULL);

	if (offset < 0)
		return -EINVAL;
	else if (offset == 0ll) {
		/*
		 * rewind to the beginning of directory
		 */
		xmemset(pos, 0, sizeof *pos);
		reiser4_stat_dir_add(readdir.reset);
		return dir_go_to(dir, pos, tap);
	}

	destination = (__u64) offset;

	shift = pos->entry_no - destination;
	if (unlikely(abs(shift) > 100000))
		/*
		 * something strange: huge seek
		 */
		warning("nikita-2549", "Strange seekdir: %llu->%llu",
			pos->entry_no, destination);
	if (shift >= 0) {
		/*
		 * rewinding to the left
		 */
		reiser4_stat_dir_add(readdir.rewind_left);
		if (shift <= (int) pos->position.pos) {
			/*
			 * destination is within sequence of entries with
			 * duplicate keys.
			 */
			pos->position.pos -= shift;
			reiser4_stat_dir_add(readdir.left_non_uniq);
			result = dir_go_to(dir, pos, tap);
		} else {
			shift -= pos->position.pos;
			pos->position.pos = 0;
			while (1) {
				/*
				 * repetitions: deadlock is possible when
				 * going to the left.
				 */
				result = dir_go_to(dir, pos, tap);
				if (result == 0) {
					result = rewind_left(tap, shift);
					if (result == -EDEADLK) {
						tap_done(tap);
						reiser4_stat_dir_add(readdir.
								     left_restart);
						continue;
					}
				}
			}
		}
	} else {
		/*
		 * rewinding to the right
		 */
		reiser4_stat_dir_add(readdir.rewind_right);
		result = dir_go_to(dir, pos, tap);
		if (result == 0)
			result = rewind_right(tap, -shift);
	}
	return result;
}

/**
 * Function that is called by common_readdir() on each directory item
 * while doing readdir.
 */
static int
feed_entry(readdir_pos * pos, coord_t * coord, filldir_t filldir, void *dirent)
{
	item_plugin *iplug;
	char *name;
	reiser4_key sd_key;
	reiser4_key de_key;
	int result;
	de_id *did;

	iplug = item_plugin_by_coord(coord);

	name = iplug->s.dir.extract_name(coord);
	assert("nikita-1371", name != NULL);
	if (iplug->s.dir.extract_key(coord, &sd_key) != 0)
		return -EIO;

	/* get key of directory entry */
	unit_key_by_coord(coord, &de_key);
	trace_on(TRACE_DIR | TRACE_VFS_OPS, "readdir: %s, %llu, %llu\n",
		 name, pos->entry_no + 1, get_key_objectid(&sd_key));

	/*
	 * update @pos
	 */
	++pos->entry_no;
	did = &pos->position.dir_entry_key;
	if (de_id_key_cmp(did, &de_key) == EQUAL_TO)
		/*
		 * we are within sequence of directory entries
		 * with duplicate keys.
		 */
		++pos->position.pos;
	else {
		pos->position.pos = 0;
		result = build_de_id_by_key(&de_key, did);
	}

	/*
	 * send information about directory entry to the ->filldir() filler
	 * supplied to us by caller (VFS).
	 */
	if (filldir(dirent, name, (int) strlen(name),
		    /*
		     * offset of the next entry
		     */
		    (loff_t) pos->entry_no + 1,
		    /*
		     * inode number of object bounden by this entry
		     */
		    oid_to_uino(get_key_objectid(&sd_key)),
		    iplug->s.dir.extract_file_type(coord)) < 0) {
		/*
		 * ->filldir() is satisfied.
		 */
		result = 1;
	} else
		result = 0;
	return result;
}

int
dir_readdir_init(struct file *f, tap_t * tap, readdir_pos ** pos)
{
	struct inode *inode;
	reiser4_file_fsdata *fsdata;
	reiser4_inode *info;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	fsdata = reiser4_get_file_fsdata(f);
	assert("nikita-2571", fsdata != NULL);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	info = reiser4_inode_data(inode);

	spin_lock(&info->guard);
	if (readdir_list_is_clean(fsdata))
		readdir_list_push_front(&info->readdir_list, fsdata);
	*pos = &fsdata->dir.readdir;
	spin_unlock(&info->guard);

	/*
	 * move @tap to the current position
	 */
	return dir_rewind(f, *pos, f->f_pos, tap);
}

/** ->readdir method of directory plugin */
static int
common_readdir(struct file *f /* directory file being read */ ,
	       void *dirent /* opaque data passed to us by VFS */ ,
	       filldir_t filld	/* filler function passed to us
				   * by VFS */ )
{
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	tap_t tap;
	file_plugin *fplug;
	readdir_pos *pos;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	reiser4_stat_dir_add(readdir.calls);

	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

	trace_on(TRACE_DIR | TRACE_VFS_OPS,
		 "readdir: inode: %llu offset: %lli\n",
		 get_inode_oid(inode), f->f_pos);

	fplug = inode_file_plugin(inode);
	result = dir_readdir_init(f, &tap, &pos);
	if (result == 0) {
		result = tap_load(&tap);
		if (result == 0)
			pos->entry_no = f->f_pos - 1;
		/*
		 * scan entries one by one feeding them to @filld
		 */
		while (result == 0) {
			coord_t *coord;

			coord = tap.coord;
			assert("nikita-2572", coord_is_existing_unit(coord));

			if (item_type_by_coord(coord) != DIR_ENTRY_ITEM_TYPE)
				break;
			else if (!fplug->owns_item(inode, coord))
				break;
			result = feed_entry(pos, coord, filld, dirent);
			if (result > 0) {
				result = 0;
				break;
			} else if (result == 0) {
				result = go_next_unit(&tap);
				if (result == -ENAVAIL || result == -ENOENT) {
					result = 0;
					break;
				}
			}
		}
		tap_relse(&tap);

		if (result == 0) {
			f->f_pos = pos->entry_no + 1;
			f->f_version = inode->i_version;
		}
	} else if (result == -ENAVAIL || result == -ENOENT)
		result = 0;
	tap_done(&tap);
	return result;
}

static int
common_attach(struct inode *child, struct inode *parent)
{
	reiser4_inode *info;
	assert("nikita-2647", child != NULL);
	assert("nikita-2648", parent != NULL);

	info = reiser4_inode_data(child);
	assert("nikita-2649",
	       (info->parent == NULL) || (info->parent == parent));
	info->parent = parent;
	return 0;
}

static reiser4_block_nr common_estimate_add_entry(struct inode *inode)
{
	reiser4_block_nr amount;
	assert("vpf-316", inode != NULL);
	
	estimate_internal_amount(1, tree_by_inode(inode)->height, &amount);

	return amount + 1;
}

static reiser4_block_nr common_estimate_rem_entry(struct inode *inode) 
{
	return 2 /* SD bytes, current node */;
}

dir_plugin dir_plugins[LAST_DIR_ID] = {
	[HASHED_DIR_PLUGIN_ID] = {
				  .h = {
					.type_id = REISER4_DIR_PLUGIN_TYPE,
					.id = HASHED_DIR_PLUGIN_ID,
					.pops = NULL,
					.label = "dir",
					.desc = "hashed directory",
					.linkage = TS_LIST_LINK_ZERO}
				  ,
				.resolve = NULL,
				.resolve_into_inode = hashed_lookup,
				.unlink = common_unlink,
				.link = common_link,
				.is_name_acceptable = is_name_acceptable,
				.entry_key = build_entry_key,
				.readdir_key = build_readdir_key,
				.add_entry = hashed_add_entry,
				.rem_entry = hashed_rem_entry,
				.create_child = common_create_child,
				.rename = hashed_rename,
				.readdir = common_readdir,
				.init = hashed_init,
				.done = hashed_done,
				.attach = common_attach,
				.estimate = {
					.create = common_estimate_create_dir,
					.add_entry = common_estimate_add_entry,
					.rem_entry = common_estimate_rem_entry,
					.link = common_estimate_link,
					.unlink = common_estimate_unlink,
					.rename	= hashed_estimate_rename,
					.init = hashed_estimate_init,
					.done = hashed_estimate_done}}
	,
	[SEEKABLE_HASHED_DIR_PLUGIN_ID] = {
				.h = {
					.type_id = REISER4_DIR_PLUGIN_TYPE,
					.id = HASHED_DIR_PLUGIN_ID,
					.pops = NULL,
					.label = "dir",
					.desc = "hashed directory",
					.linkage = TS_LIST_LINK_ZERO}
				,
				.resolve = NULL,
				.resolve_into_inode = hashed_lookup,
				.unlink = common_unlink,
				.link = common_link,
				.is_name_acceptable = is_name_acceptable,
				.entry_key = build_readdir_stable_entry_key,
				.readdir_key = build_readdir_key,
				.add_entry = hashed_add_entry,
				.rem_entry = hashed_rem_entry,
				.create_child = common_create_child,
				.rename = hashed_rename,
				.readdir = common_readdir,
				.init = hashed_init,
				.done = hashed_done,
				.attach = common_attach,
				.estimate = {
					.create = common_estimate_create_dir,
					.add_entry = common_estimate_add_entry,
					.rem_entry = common_estimate_rem_entry,
					.link = common_estimate_link,
					.unlink = common_estimate_unlink,
					.rename	= hashed_estimate_rename,
					.init = hashed_estimate_init,
					.done = hashed_estimate_done}}
	,
};

/* 
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
