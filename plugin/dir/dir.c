/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Methods of directory plugin. */

#include "../../forward.h"
#include "../../debug.h"
#include "../../spin_macros.h"
#include "../plugin_header.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../type_safe_list.h"
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

#include "hashed_dir.h"
#include "pseudo_dir.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct file  */
#include <linux/quotaops.h>
#include <linux/dcache.h>	/* for struct dentry */

/* helper function. Standards require than for many file-system operations
   on success ctime and mtime of parent directory is to be updated. */
int
reiser4_update_dir(struct inode *dir)
{
	assert("nikita-2525", dir != NULL);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	return reiser4_mark_inode_dirty(dir);
}

/* estimate disk space necessary to add a link from @parent to @object. */
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

/* add link from @parent directory to @existing object.

       . get plugins
       . check permissions
       . check that "existing" can hold yet another link
       . start transaction
       . add link to "existing"
       . add entry to "parent"
       . if last step fails, remove link from "existing"

*/
static int
link_common(struct inode *parent /* parent directory */ ,
	    struct dentry *existing	/* dentry of object to which
					 * new link is being
					 * cerated */ ,
	    struct dentry *newname /* new name */ )
{
	int result;
	struct inode *object;
	dir_plugin *parent_dplug;
	reiser4_dir_entry_desc entry;
	reiser4_object_create_data data;
	reiser4_block_nr reserve;

	assert("nikita-1431", existing != NULL);
	assert("nikita-1432", parent != NULL);
	assert("nikita-1433", newname != NULL);

	object = existing->d_inode;
	assert("nikita-1434", object != NULL);

	/* check for race with create_object() */
	if (inode_get_flag(object, REISER4_IMMUTABLE))
		return RETERR(-E_REPEAT);

	/* links to directories are not allowed if file-system
	   logical name-space should be ADG */
	if (S_ISDIR(object->i_mode) && reiser4_is_set(parent->i_sb, REISER4_ADG))
		return RETERR(-EISDIR);

	/* check permissions */
	result = perm_chk(parent, link, existing, parent, newname);
	if (result != 0)
		return result;

	parent_dplug = inode_dir_plugin(parent);

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	data.mode = object->i_mode;
	data.id = inode_file_plugin(object)->h.id;

	reserve = common_estimate_link(parent, existing->d_inode);
	if ((__s64)reserve < 0)
	    return reserve;

	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
	    return RETERR(-ENOSPC);

	result = reiser4_add_nlink(object, parent, 1);
	if (result == 0) {
		/* add entry to the parent */
		result = parent_dplug->add_entry(parent, newname, &data, &entry);
		if (result != 0) {
			/* failure to add entry to the parent, remove
			   link from "existing" */
			reiser4_del_nlink(object, parent, 1);
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
		/* Upon successful completion, link() shall mark for update
		   the st_ctime field of the file. Also, the st_ctime and
		   st_mtime fields of the directory that contains the new
		   entry shall be marked for update. --SUS
		*/
		result = reiser4_update_dir(parent);
	}
	return result;
}

/* estimate disk space necessary to remove a link between @parent and
 * @object. */
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
	/* fplug->unlink */
	res += fplug->estimate.unlink(object, parent);

	return res;
}

/* grab space for unlink. */
static int
unlink_check_and_grab(struct inode *parent, struct dentry *victim)
{
	file_plugin  *fplug;
	struct inode *child;
	int           result;

	result = 0;
	child = victim->d_inode;
	fplug = inode_file_plugin(child);

	/* check for race with create_object() */
	if (inode_get_flag(child, REISER4_IMMUTABLE))
		return RETERR(-E_REPEAT);
	/* object being deleted should have stat data */
	assert("vs-949", !inode_get_flag(child, REISER4_NO_SD));

	/* check permissions */
	result = perm_chk(parent, unlink, parent, victim);
	if (result != 0)
		return result;

	/* ask object plugin */
	if (fplug->can_rem_link != NULL && !fplug->can_rem_link(child))
		return RETERR(-ENOTEMPTY);

	result = (int)common_estimate_unlink(parent, child);
	if (result < 0)
		return result;

	return reiser4_grab_reserved(child->i_sb, result, BA_CAN_COMMIT);
}

/* remove link from @parent directory to @victim object.

       . get plugins
       . find entry in @parent
       . check permissions
       . decrement nlink on @victim
       . if nlink drops to 0, delete object
*/
static int
unlink_common(struct inode *parent /* parent object */ ,
	      struct dentry *victim /* name being removed from @parent */)
{
	int           result;
	struct inode *object;
	file_plugin  *fplug;

	object = victim->d_inode;
	fplug  = inode_file_plugin(object);
	assert("nikita-2882", fplug->detach != NULL);

	result = unlink_check_and_grab(parent, victim);
	if (result == 0 && (result = fplug->detach(object, parent)) == 0) {
		dir_plugin            *parent_dplug;
		reiser4_dir_entry_desc entry;

		parent_dplug = inode_dir_plugin(parent);
		xmemset(&entry, 0, sizeof entry);

		/* first, delete directory entry */
		result = parent_dplug->rem_entry(parent, victim, &entry);
		if (result == 0) {
			/* now that directory entry is removed, update
			 * stat-data */
			result = reiser4_del_nlink(object, parent, 1);
			if (result == 0)
				/* Upon successful completion, unlink() shall
				   mark for update the st_ctime and st_mtime
				   fields of the parent directory. Also, if
				   the file's link count is not 0, the
				   st_ctime field of the file shall be marked
				   for update. --SUS */
				result = reiser4_update_dir(parent);
		}
		if (unlikely(result != 0))
			warning("nikita-3398", "Cannot unlink %llu (%i)",
				get_inode_oid(object), result);
	}
	reiser4_release_reserved(object->i_sb);

	/* @object's i_ctime was updated by ->rem_link() method(). */
	return result;
}

/* Estimate the maximum amount of nodes will be allocated or changed for:
   - insert an in the parent entry
   - update the SD of parent
   - estimate child creation
*/
static reiser4_block_nr common_estimate_create_child(
	struct inode *parent, /* parent object */
	struct inode *object /* object */)
{
	assert("vpf-309", parent != NULL);
	assert("vpf-307", object != NULL);
	
	return
		/* object creation estimation */
		inode_file_plugin(object)->estimate.create(object) +
		/* stat data of parent directory estimation */
		inode_file_plugin(parent)->estimate.update(parent) +
		/* adding entry estimation */
		inode_dir_plugin(parent)->estimate.add_entry(parent) +
		/* to undo in the case of failure */
		inode_dir_plugin(parent)->estimate.rem_entry(parent);
}

/* Create child in directory.

   . get object's plugin
   . get fresh inode
   . initialize inode
   . add object's stat-data
   . initialize object's directory
   . add entry to the parent
   . instantiate dentry

*/
/* ->create_child method of directory plugin */
static int
create_child_common(reiser4_object_create_data * data	/* parameters
							 * of new
							 * object */,
		    struct inode ** retobj)
{
	int result;

	struct dentry *dentry;	/* parent object */
	struct inode *parent;	/* new name */

	dir_plugin *par_dir;	/* directory plugin on the parent */
	dir_plugin *obj_dir;	/* directory plugin on the new object */
	file_plugin *obj_plug;	/* object plugin on the new object */
	struct inode *object;	/* new object */
	reiser4_block_nr reserve;

	reiser4_dir_entry_desc entry;	/* new directory entry */

	assert("nikita-1420", data != NULL);
	parent = data->parent;
	dentry = data->dentry;

	assert("nikita-1418", parent != NULL);
	assert("nikita-1419", dentry != NULL);
	par_dir = inode_dir_plugin(parent);
	/* check permissions */
	result = perm_chk(parent, create, parent, dentry, data);
	if (result != 0)
		return result;

	/* check, that name is acceptable for parent */
	if (par_dir->is_name_acceptable &&
	    !par_dir->is_name_acceptable(parent,
					 dentry->d_name.name,
					 (int) dentry->d_name.len))
		return RETERR(-ENAMETOOLONG);

	result = 0;
	obj_plug = file_plugin_by_id((int) data->id);
	if (obj_plug == NULL) {
		warning("nikita-430", "Cannot find plugin %i", data->id);
		return RETERR(-ENOENT);
	}
	object = new_inode(parent->i_sb);
	if (object == NULL)
		return RETERR(-ENOMEM);
	/* we'll update i_nlink below */
	object->i_nlink = 0;
	/* new_inode() initializes i_ino to "arbitrary" value. Reset it to 0,
	 * to simplify error handling: if some error occurs before i_ino is
	 * initialized with oid, i_ino should already be set to some
	 * distinguished value. */
	object->i_ino = 0;

	/* So that on error iput will be called. */
	*retobj = object;

	if (DQUOT_ALLOC_INODE(object)) {
		DQUOT_DROP(object);
		object->i_flags |= S_NOQUOTA;
		return RETERR(-EDQUOT);
	}

	xmemset(&entry, 0, sizeof entry);
	entry.obj = object;

	plugin_set_file(&reiser4_inode_data(object)->pset, obj_plug);
	result = obj_plug->set_plug_in_inode(object, parent, data);
	if (result) {
		warning("nikita-431", "Cannot install plugin %i on %llx", data->id, get_inode_oid(object));
		return result;
	}

	/* reget plugin after installation */
	obj_plug = inode_file_plugin(object);

	if (obj_plug->create == NULL)
		return RETERR(-EPERM);

	/* if any of hash, tail, sd or permission plugins for newly created
	   object are not set yet set them here inheriting them from parent
	   directory
	*/
	assert("nikita-2070", obj_plug->adjust_to_parent != NULL);
	result = obj_plug->adjust_to_parent(object,
					    parent,
					    object->i_sb->s_root->d_inode);
	if (result != 0) {
		warning("nikita-432", "Cannot inherit from %llx to %llx",
			get_inode_oid(parent), get_inode_oid(object));
		return result;
	}

	/* call file plugin's method to initialize plugin specific part of
	 * inode */
	if (obj_plug->init_inode_data)
		obj_plug->init_inode_data(object, data, 1/*create*/);

	/* obtain directory plugin (if any) for new object. */
	obj_dir = inode_dir_plugin(object);
	if ((obj_dir != NULL) && (obj_dir->init == NULL))
		return RETERR(-EPERM);

	reiser4_inode_data(object)->locality_id = get_inode_oid(parent);

	reserve = common_estimate_create_child(parent, object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT))
		return RETERR(-ENOSPC);

	/* mark inode `immutable'. We disable changes to the file being
	   created until valid directory entry for it is inserted. Otherwise,
	   if file were expanded and insertion of directory entry fails, we
	   have to remove file, but we only alloted enough space in
	   transaction to remove _empty_ file. 3.x code used to remove stat
	   data in different transaction thus possibly leaking disk space on
	   crash. This all only matters if it's possible to access file
	   without name, for example, by inode number
	*/
	inode_set_flag(object, REISER4_IMMUTABLE);

	/* create empty object, this includes allocation of new objectid. For
	   directories this implies creation of dot and dotdot  */
	assert("nikita-2265", inode_get_flag(object, REISER4_NO_SD));

	/* mark inode as `loaded'. From this point onward
	   reiser4_delete_inode() will try to remove its stat-data. */
	inode_set_flag(object, REISER4_LOADED);

	result = obj_plug->create(object, parent, data);
	if (result != 0) {
		inode_clr_flag(object, REISER4_IMMUTABLE);
		if (result != -ENAMETOOLONG)
			warning("nikita-2219",
				"Failed to create sd for %llu",
				get_inode_oid(object));
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
			/* If O_CREAT is set and the file did not previously
			   exist, upon successful completion, open() shall
			   mark for update the st_atime, st_ctime, and
			   st_mtime fields of the file and the st_ctime and
			   st_mtime fields of the parent directory. --SUS
			*/
			/* @object times are already updated by
			   reiser4_add_nlink() */
			if (result == 0) {
				result = reiser4_update_dir(parent);
				if (result != 0)
					reiser4_del_nlink(object, parent, 1);
			}
			if (result != 0)
				/* cleanup failure to update times */
				par_dir->rem_entry(parent, dentry, &entry);
		}
		if (result != 0)
			/* cleanup failure to add entry */
			if (obj_dir != NULL)
				obj_dir->done(object);
	} else
		warning("nikita-2219", "Failed to initialize dir for %llu: %i",
			get_inode_oid(object), result);

	if (result != 0)
		/* failure to create entry, remove object */
		obj_plug->delete(object);

	/* file has name now, clear immutable flag */
	inode_clr_flag(object, REISER4_IMMUTABLE);

	/* on error, iput() will call ->delete_inode(). We should keep track
	   of the existence of stat-data for this inode and avoid attempt to
	   remove it in reiser4_delete_inode(). This is accomplished through
	   REISER4_NO_SD bit in inode.u.reiser4_i.plugin.flags
	*/
	return result;
}

/* ->is_name_acceptable() method of directory plugin */
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

static int
is_valid_dir_coord(struct inode * inode, coord_t * coord)
{
	return
		item_type_by_coord(coord) == DIR_ENTRY_ITEM_TYPE &&
		inode_file_plugin(inode)->owns_item(inode, coord);
}

/* true if directory is empty (only contains dot and dotdot) */
int
is_dir_empty(const struct inode *dir)
{
	assert("nikita-1976", dir != NULL);

	/* rely on our method to maintain directory i_size being equal to the
	   number of entries. */
	return dir->i_size <= 2 ? 0 : RETERR(-ENOTEMPTY);
}

/* compare two logical positions within the same directory */
cmp_t dir_pos_cmp(const dir_pos * p1, const dir_pos * p2)
{
	cmp_t result;

	assert("nikita-2534", p1 != NULL);
	assert("nikita-2535", p2 != NULL);

	result = de_id_cmp(&p1->dir_entry_key, &p2->dir_entry_key);
	if (result == EQUAL_TO) {
		int diff;

		diff = p1->pos - p2->pos;
		result = (diff < 0) ? LESS_THAN : (diff ? GREATER_THAN : EQUAL_TO);
	}
	return result;
}


#if REISER4_DEBUG_OUTPUT && REISER4_TRACE
static char filter(const d8 *dch)
{
	char ch;

	ch = d8tocpu(dch);
	if (' ' <= ch && ch <= '~')
		return ch;
	else
		return '?';
}

static void
print_de_id(const char *prefix, const de_id *did)
{
	reiser4_key key;

	extract_key_from_de_id(0, did, &key);
	print_key(prefix, &key);
	return;
	printk("%s: %c%c%c%c%c%c%c%c:%c%c%c%c%c%c%c%c",
	       prefix,
	       filter(&did->objectid[0]),
	       filter(&did->objectid[1]),
	       filter(&did->objectid[2]),
	       filter(&did->objectid[3]),
	       filter(&did->objectid[4]),
	       filter(&did->objectid[5]),
	       filter(&did->objectid[6]),
	       filter(&did->objectid[7]),

	       filter(&did->offset[0]),
	       filter(&did->offset[1]),
	       filter(&did->offset[2]),
	       filter(&did->offset[3]),
	       filter(&did->offset[4]),
	       filter(&did->offset[5]),
	       filter(&did->offset[6]),
	       filter(&did->offset[7]));
}

static void
print_dir_pos(const char *prefix, const dir_pos *pos)
{
	print_de_id(prefix, &pos->dir_entry_key);
	printk(" pos: %u", pos->pos);
}

#else
#define print_de_id(p, did) noop
#define print_dir_pos(prefix, pos) noop
#endif

/* see comment before readdir_common() for overview of why "adjustment" is
 * necessary. */
static void
adjust_dir_pos(struct file   * dir,
	       readdir_pos   * readdir_spot,
	       const dir_pos * mod_point,
	       int             adj)
{
	dir_pos *pos;

	/*
	 * new directory entry was added (adj == +1) or removed (adj == -1) at
	 * the @mod_point. Directory file descriptor @dir is doing readdir and
	 * is currently positioned at @readdir_spot. Latter has to be updated
	 * to maintain stable readdir.
	 */

	ON_TRACE(TRACE_DIR, "adjust: %s/%i", dir->f_dentry->d_name.name, adj);
	IF_TRACE(TRACE_DIR, print_dir_pos("\n mod", mod_point));
	IF_TRACE(TRACE_DIR, print_dir_pos("\nspot", &readdir_spot->position));
	ON_TRACE(TRACE_DIR, "\nf_pos: %llu, spot.entry_no: %llu\n",
		 dir->f_pos, readdir_spot->entry_no);

	reiser4_stat_inc(dir.readdir.adjust_pos);

	/* directory is positioned to the beginning. */
	if (dir->f_pos == 0)
		return;

	pos = &readdir_spot->position;
	switch (dir_pos_cmp(mod_point, pos)) {
	case LESS_THAN:
		/* @mod_pos is _before_ @readdir_spot, that is, entry was
		 * added/removed on the left (in key order) of current
		 * position. */
		readdir_spot->entry_no += adj;
		assert("nikita-2577", dir->f_pos + adj >= 0);
		/* logical number of directory entry readdir is "looking" at
		 * changes */
		dir->f_pos += adj;
		if (de_id_cmp(&pos->dir_entry_key, &mod_point->dir_entry_key) == EQUAL_TO) {
			assert("nikita-2575", mod_point->pos < pos->pos);
			/*
			 * if entry added/removed has the same key as current
			 * for readdir, update counter of duplicate keys in
			 * @readdir_spot.
			 */
			pos->pos += adj;
		}
		reiser4_stat_inc(dir.readdir.adjust_lt);
		break;
	case GREATER_THAN:
		/* directory is modified after @pos: nothing to do. */
		reiser4_stat_inc(dir.readdir.adjust_gt);
		break;
	case EQUAL_TO:
		/* cannot insert an entry readdir is looking at, because it
		   already exists. */
		assert("nikita-2576", adj < 0);
		/* directory entry to which @pos points to is being
		   removed.
		
		   NOTE-NIKITA: Right thing to do is to update @pos to point
		   to the next entry. This is complex (we are under spin-lock
		   for one thing). Just rewind it to the beginning. Next
		   readdir will have to scan the beginning of
		   directory. Proper solution is to use semaphore in
		   spin lock's stead and use rewind_right() here.

		   NOTE-NIKITA: now, semaphore is used, so...
		*/
		xmemset(readdir_spot, 0, sizeof *readdir_spot);
		reiser4_stat_inc(dir.readdir.adjust_eq);
	}
}

/* scan all file-descriptors for this directory and adjust their positions
   respectively. */
void
adjust_dir_file(struct inode *dir, const struct dentry * de, int offset, int adj)
{
	reiser4_file_fsdata *scan;
	dir_pos mod_point;

	assert("nikita-2536", dir != NULL);
	assert("nikita-2538", de  != NULL);
	assert("nikita-2539", adj != 0);

	build_de_id(dir, &de->d_name, &mod_point.dir_entry_key);
	mod_point.pos = offset;

	spin_lock_inode(dir);

	/*
	 * new entry was added/removed in directory @dir. Scan all file
	 * descriptors for @dir that are currently involved into @readdir and
	 * update them.
	 */

	for_all_type_safe_list(readdir, get_readdir_list(dir), scan)
		adjust_dir_pos(scan->back, &scan->dir.readdir, &mod_point, adj);

	spin_unlock_inode(dir);
}

static int
dir_go_to(struct file *dir, readdir_pos * pos, tap_t * tap)
{
	reiser4_key key;
	int result;
	struct inode *inode;

	assert("nikita-2554", pos != NULL);

	inode = dir->f_dentry->d_inode;
	result = inode_dir_plugin(inode)->build_readdir_key(dir, &key);
	if (result != 0)
		return result;
	result = object_lookup(inode,
			       &key,
			       tap->coord,
			       tap->lh,
			       tap->mode,
			       FIND_EXACT,
			       LEAF_LEVEL,
			       LEAF_LEVEL,
			       0,
			       &tap->ra_info);
	if (result == CBK_COORD_FOUND)
		result = rewind_right(tap, (int) pos->position.pos);
	else {
		tap->coord->node = NULL;
		done_lh(tap->lh);
		result = RETERR(-EIO);
	}
	return result;
}

static int
set_pos(struct inode * inode, readdir_pos * pos, tap_t * tap)
{
	int          result;
	coord_t      coord;
	lock_handle  lh;
	tap_t        scan;
	de_id       *did;
	reiser4_key  de_key;

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&scan, &coord, &lh, ZNODE_READ_LOCK);
	tap_copy(&scan, tap);
	tap_load(&scan);
	pos->position.pos = 0;

	did = &pos->position.dir_entry_key;

	if (is_valid_dir_coord(inode, scan.coord)) {

		build_de_id_by_key(unit_key_by_coord(scan.coord, &de_key), did);

		while (1) {

			result = go_prev_unit(&scan);
			if (result != 0)
				break;

			if (!is_valid_dir_coord(inode, scan.coord)) {
				result = -EINVAL;
				break;
			}

			/* get key of directory entry */
			unit_key_by_coord(scan.coord, &de_key);
			if (de_id_key_cmp(did, &de_key) != EQUAL_TO) {
				/* duplicate-sequence is over */
				break;
			}
			pos->position.pos ++;
		}
	} else
		result = RETERR(-ENOENT);
	tap_relse(&scan);
	tap_done(&scan);
	return result;
}


/*
 * "rewind" directory to @offset, i.e., set @pos and @tap correspondingly.
 */
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
		return RETERR(-EINVAL);
	else if (offset >= dir->f_dentry->d_inode->i_size)
		return RETERR(-ENOENT);
	else if (offset == 0ll) {
		/* rewind to the beginning of directory */
		xmemset(pos, 0, sizeof *pos);
		reiser4_stat_inc(dir.readdir.reset);
		return dir_go_to(dir, pos, tap);
	}

	destination = (__u64) offset;

	shift = pos->entry_no - destination;
	if (shift >= 0) {
		/* rewinding to the left */
		reiser4_stat_inc(dir.readdir.rewind_left);
		if (shift <= (int) pos->position.pos) {
			/* destination is within sequence of entries with
			   duplicate keys. */
			reiser4_stat_inc(dir.readdir.left_non_uniq);
			result = dir_go_to(dir, pos, tap);
		} else {
			shift -= pos->position.pos;
			while (1) {
				/* repetitions: deadlock is possible when
				   going to the left. */
				result = dir_go_to(dir, pos, tap);
				if (result == 0) {
					result = rewind_left(tap, shift);
					if (result == -E_DEADLOCK) {
						tap_done(tap);
						reiser4_stat_inc(dir.readdir.left_restart);
						continue;
					}
				}
				break;
			}
		}
	} else {
		/* rewinding to the right */
		reiser4_stat_inc(dir.readdir.rewind_right);
		result = dir_go_to(dir, pos, tap);
		if (result == 0)
			result = rewind_right(tap, -shift);
	}
	if (result == 0) {
		result = set_pos(dir->f_dentry->d_inode, pos, tap);
		if (result == 0)
			/* update pos->position.pos */
			pos->entry_no = destination;
	}
	return result;
}

/* Function that is called by common_readdir() on each directory item
   while doing readdir. */
static int
feed_entry(readdir_pos * pos, coord_t * coord, filldir_t filldir, void *dirent)
{
	item_plugin *iplug;
	char *name;
	reiser4_key sd_key;
	int result;
	char buf[DE_NAME_BUF_LEN];

	iplug = item_plugin_by_coord(coord);

	name = iplug->s.dir.extract_name(coord, buf);
	assert("nikita-1371", name != NULL);
	if (iplug->s.dir.extract_key(coord, &sd_key) != 0)
		return RETERR(-EIO);

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS, "readdir: %s, %llu, %llu\n",
		 name, pos->entry_no + 1, get_key_objectid(&sd_key));

	/* send information about directory entry to the ->filldir() filler
	   supplied to us by caller (VFS). */
	if (filldir(dirent, name, (int) strlen(name),
		    /* offset of the next entry */
		    (loff_t) pos->entry_no,
		    /* inode number of object bounden by this entry */
		    oid_to_uino(get_key_objectid(&sd_key)),
		    iplug->s.dir.extract_file_type(coord)) < 0) {
		/* ->filldir() is satisfied. */
		result = 1;
	} else
		result = 0;
	return result;
}

static void
move_entry(readdir_pos * pos, coord_t * coord)
{
	reiser4_key de_key;
	de_id *did;

	/* update @pos */
	++pos->entry_no;
	did = &pos->position.dir_entry_key;

	/* get key of directory entry */
	unit_key_by_coord(coord, &de_key);

	if (de_id_key_cmp(did, &de_key) == EQUAL_TO)
		/* we are within sequence of directory entries
		   with duplicate keys. */
		++pos->position.pos;
	else {
		pos->position.pos = 0;
		build_de_id_by_key(&de_key, did);
	}
}

int
dir_readdir_init(struct file *f, tap_t * tap, readdir_pos ** pos)
{
	struct inode *inode;
	reiser4_file_fsdata *fsdata;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	fsdata = reiser4_get_file_fsdata(f);
	assert("nikita-2571", fsdata != NULL);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	spin_lock_inode(inode);
	if (readdir_list_is_clean(fsdata))
		readdir_list_push_front(get_readdir_list(inode), fsdata);
	*pos = &fsdata->dir.readdir;
	spin_unlock_inode(inode);

	IF_TRACE(TRACE_DIR, print_dir_pos("readdir", &(*pos)->position));
	ON_TRACE(TRACE_DIR, " entry_no: %llu\n", (*pos)->entry_no);

	/* move @tap to the current position */
	return dir_rewind(f, *pos, f->f_pos, tap);
}

/*
 * ->readdir method of directory plugin
 *
 * readdir problems:
 *
 *     Traditional UNIX API for scanning through directory
 *     (readdir/seekdir/telldir/opendir/closedir/rewindir/getdents) is based
 *     on the assumption that directory is structured very much like regular
 *     file, in particular, it is implied that each name within given
 *     directory (directory entry) can be uniquely identified by scalar offset
 *     and that such offset is stable across the life-time of the name is
 *     identifies.
 *
 *     This is manifestly not so for reiser4. In reiser4 the only stable
 *     unique identifies for the directory entry is its key that doesn't fit
 *     into seekdir/telldir API.
 *
 * solution:
 *
 *     Within each file descriptor participating in readdir-ing of directory
 *     plugin/dir/dir.h:readdir_pos is maintained. This structure keeps track
 *     of the "current" directory entry that file descriptor looks at. It
 *     contains a key of directory entry (plus some additional info to deal
 *     with non-unique keys that we wouldn't dwell onto here) and a logical
 *     position of this directory entry starting from the beginning of the
 *     directory, that is ordinal number of this entry in the readdir order.
 *
 *     Obviously this logical position is not stable in the face of directory
 *     modifications. To work around this, on each addition or removal of
 *     directory entry all file descriptors for directory inode are scanned
 *     and their readdir_pos are updated accordingly (adjust_dir_pos()).
 *
 */
static int
readdir_common(struct file *f /* directory file being read */ ,
	       void *dirent /* opaque data passed to us by VFS */ ,
	       filldir_t filld	/* filler function passed to us
				   * by VFS */ )
{
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	tap_t tap;
	readdir_pos *pos;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	reiser4_stat_inc(dir.readdir.calls);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

	/* initialize readdir readahead information: include into readahead stat data of all files of the directory */
	set_key_locality(&tap.ra_info.key_to_stop, get_inode_oid(inode));
	set_key_type(&tap.ra_info.key_to_stop, KEY_SD_MINOR);
	set_key_ordering(&tap.ra_info.key_to_stop, get_key_ordering(max_key()));
	set_key_objectid(&tap.ra_info.key_to_stop, get_key_objectid(max_key()));
	set_key_offset(&tap.ra_info.key_to_stop, get_key_offset(max_key()));

	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
		 "readdir: inode: %llu offset: %lli\n",
		 get_inode_oid(inode), f->f_pos);

	result = dir_readdir_init(f, &tap, &pos);
	if (result == 0) {
		result = tap_load(&tap);
		/* scan entries one by one feeding them to @filld */
		while (result == 0) {
			coord_t *coord;

			coord = tap.coord;
			assert("nikita-2572", coord_is_existing_unit(coord));
			assert("nikita-3227", is_valid_dir_coord(inode, coord));

			result = feed_entry(pos, coord, filld, dirent);
			ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
				 "readdir: entry: offset: %lli\n", f->f_pos);
			if (result > 0) {
				break;
			} else if (result == 0) {
				++ f->f_pos;
				result = go_next_unit(&tap);
				if (result == -E_NO_NEIGHBOR || result == -ENOENT) {
					result = 0;
					break;
				} else if (result == 0) {
					if (is_valid_dir_coord(inode, coord))
						move_entry(pos, coord);
					else
						break;
				}
			}
		}
		tap_relse(&tap);

		if (result >= 0)
			f->f_version = inode->i_version;
	} else if (result == -E_NO_NEIGHBOR || result == -ENOENT)
		result = 0;
	tap_done(&tap);
	ON_TRACE(TRACE_DIR | TRACE_VFS_OPS,
		 "readdir_exit: offset: %lli\n", f->f_pos);
	return result;
}

/* ->attach method of directory plugin */
static int
attach_common(struct inode *child UNUSED_ARG, struct inode *parent UNUSED_ARG)
{
	assert("nikita-2647", child != NULL);
	assert("nikita-2648", parent != NULL);

	return 0;
}

/* ->estimate.add_entry method of directory plugin
   estimation of adding entry which supposes that entry is inserting a unit into item
*/
static reiser4_block_nr
estimate_add_entry_common(struct inode *inode)
{
	return estimate_one_insert_into_item(tree_by_inode(inode));
}

/* ->estimate.rem_entry method of directory plugin */
static reiser4_block_nr
estimate_rem_entry_common(struct inode *inode)
{
	return estimate_one_item_removal(tree_by_inode(inode));
}

static ssize_t
perm(void)
{
	return RETERR(-EPERM);
}

#define eperm ((void *)perm)

static int
_noop(void)
{
	return 0;
}

#define enoop ((void *)_noop)

/*
 * definition of directory plugins
 */

dir_plugin dir_plugins[LAST_DIR_ID] = {
	/* standard hashed directory plugin */
	[HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = HASHED_DIR_PLUGIN_ID,
			.pops = NULL,
			.label = "dir",
			.desc = "hashed directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.lookup = lookup_hashed,
		.unlink = unlink_common,
		.link = link_common,
		.is_name_acceptable = is_name_acceptable,
		.build_entry_key = build_entry_key_common,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_hashed,
		.rem_entry = rem_entry_hashed,
		.create_child = create_child_common,
		.rename = rename_hashed,
		.readdir = readdir_common,
		.init = init_hashed,
		.done = done_hashed,
		.attach = attach_common,
		.detach = detach_hashed,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink    = estimate_unlink_hashed
		}
	},
	/* hashed directory for which seekdir/telldir are guaranteed to
	 * work. Brain-damage. */
	[SEEKABLE_HASHED_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = SEEKABLE_HASHED_DIR_PLUGIN_ID,
			.pops = NULL,
			.label = "dir",
			.desc = "hashed directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.lookup = lookup_pseudo,
		.unlink = unlink_common,
		.link = link_common,
		.is_name_acceptable = is_name_acceptable,
		.build_entry_key = build_entry_key_stable_entry,
		.build_readdir_key = build_readdir_key_common,
		.add_entry = add_entry_hashed,
		.rem_entry = rem_entry_hashed,
		.create_child = create_child_common,
		.rename = rename_hashed,
		.readdir = readdir_common,
		.init = init_hashed,
		.done = done_hashed,
		.attach = attach_common,
		.detach = detach_hashed,
		.estimate = {
			.add_entry = estimate_add_entry_common,
			.rem_entry = estimate_rem_entry_common,
			.unlink    = estimate_unlink_hashed
		}
	},
	/* pseudo directory. */
	[PSEUDO_DIR_PLUGIN_ID] = {
		.h = {
			.type_id = REISER4_DIR_PLUGIN_TYPE,
			.id = PSEUDO_DIR_PLUGIN_ID,
			.pops = NULL,
			.label = "pseudo",
			.desc = "pseudo directory",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.lookup = lookup_pseudo,
		.unlink = eperm,
		.link = eperm,
		.is_name_acceptable = NULL,
		.build_entry_key = NULL,
		.build_readdir_key = NULL,
		.add_entry = eperm,
		.rem_entry = eperm,
		.create_child = NULL,
		.rename = eperm,
		.readdir = readdir_pseudo,
		.init = enoop,
		.done = enoop,
		.attach = enoop,
		.detach = enoop,
		.estimate = {
			.add_entry = NULL,
			.rem_entry = NULL,
			.unlink    = NULL
		}
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
