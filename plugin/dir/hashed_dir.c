/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Directory plugin using hashes (see fs/reiser4/plugin/hash.c) to map
   file names to the files. */

#include "../../forward.h"
#include "../../debug.h"
#include "../../spin_macros.h"
#include "../../key.h"
#include "../../kassign.h"
#include "../../coord.h"
#include "../../seal.h"
#include "dir.h"
#include "../item/item.h"
#include "..//security/perm.h"
#include "../plugin.h"
#include "../object.h"
#include "../../jnode.h"
#include "../../znode.h"
#include "../../tree.h"
#include "../../vfs_ops.h"
#include "../../inode.h"
#include "../../reiser4.h"

#include <linux/fs.h>		/* for struct inode */
#include <linux/dcache.h>	/* for struct dentry */

static int create_dot_dotdot(struct inode *object, struct inode *parent);
static int find_entry(const struct inode *dir, struct dentry *name,
		      lock_handle * lh, znode_lock_mode mode, 
		      reiser4_dir_entry_desc * entry);
static int check_item(const struct inode *dir, const coord_t * coord, const char *name);
reiser4_block_nr hashed_estimate_init(struct inode *parent, struct inode *object)
{
	reiser4_block_nr res = 0;
    
	assert("vpf-321", parent != NULL);
	assert("vpf-322", object != NULL);	
	
	/* hashed_add_entry(object) */
	res += inode_dir_plugin(object)->estimate.add_entry(object);
	/* reiser4_add_nlink(object) */
	res += inode_file_plugin(object)->estimate.update(object);
	/* hashed_add_entry(object) */
	res += inode_dir_plugin(object)->estimate.add_entry(object);
	/* reiser4_add_nlink(parent) */
	res += inode_file_plugin(parent)->estimate.update(parent);

	return 0;
}

/* create sd for directory file. Create stat-data, dot, and dotdot. */
int
hashed_init(struct inode *object /* new directory */ ,
	    struct inode *parent /* parent directory */ ,
	    reiser4_object_create_data * data UNUSED_ARG	/* info passed
								 * to us, this
								 * is filled by
								 * reiser4()
								 * syscall in
								 * particular */ )
{
	reiser4_block_nr reserve;

	assert("nikita-680", object != NULL);
	assert("nikita-681", S_ISDIR(object->i_mode));
	assert("nikita-682", parent != NULL);
	assert("nikita-684", data != NULL);
	assert("nikita-686", data->id == DIRECTORY_FILE_PLUGIN_ID);
	assert("nikita-687", object->i_mode & S_IFDIR);
	trace_stamp(TRACE_DIR);

	reserve = hashed_estimate_init(parent, object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT, "hashed_init: reserve for creating \".\" and \"..\"?"))
		return -ENOSPC;
	
	return create_dot_dotdot(object, parent);
}

static reiser4_block_nr 
hashed_estimate_done(struct inode *object) 
{
	reiser4_block_nr res = 0;
	
	/* hashed_rem_entry(object) */
	res += inode_dir_plugin(object)->estimate.rem_entry(object);
	return res;
}

reiser4_block_nr 
hashed_estimate_detach(struct inode *parent, struct inode *object) 
{
	reiser4_block_nr res = 0;
	
	/* hashed_rem_entry(object) */
	res += inode_dir_plugin(object)->estimate.rem_entry(object);
	/* del_nlink(parent) */
	res += 2 * inode_file_plugin(parent)->estimate.update(parent);

	return res;
}

/* ->delete() method of directory plugin
  
   Delete dot, and call common_file_delete() to delete stat data.
*/
int
hashed_done(struct inode *object /* object being deleted */)
{
	int result;
	reiser4_block_nr reserve;
	struct dentry goodby_dots;
	reiser4_dir_entry_desc entry;

	assert("nikita-1449", object != NULL);

	if (inode_get_flag(object, REISER4_NO_SD))
		return 0;

	/* of course, this can be rewritten to sweep everything in one
	   cut_tree(). */
	xmemset(&entry, 0, sizeof entry);

	reserve = hashed_estimate_done(object);
	if (reiser4_grab_space(reserve, 
			       BA_CAN_COMMIT | BA_RESERVED, "hashed_done")) 
		return -ENOSPC;
				
	xmemset(&goodby_dots, 0, sizeof goodby_dots);
	entry.obj = goodby_dots.d_inode = object;
	goodby_dots.d_name.name = ".";
	goodby_dots.d_name.len = 1;
	result = hashed_rem_entry(object, &goodby_dots, &entry);
	reiser4_free_dentry_fsdata(&goodby_dots);
	if (result != 0)
		/* only worth a warning
			  
         		"values of B will give rise to dom!\n"
		             -- v6src/s2/mv.c:89
		*/
		warning("nikita-2252", "Cannot remove dot of %lli: %i", 
			get_inode_oid(object), result);
	return 0;
}

/* ->detach() method of directory plugin
  
   Delete dotdot, decrease nlink on parent
*/
int
hashed_detach(struct inode *object, struct inode *parent)
{
	int result;
	struct dentry goodby_dots;
	reiser4_dir_entry_desc entry;

	assert("nikita-2885", object != NULL);
	assert("nikita-2886", !inode_get_flag(object, REISER4_NO_SD));

	xmemset(&entry, 0, sizeof entry);

	/* NOTE-NIKITA this only works if @parent is -the- parent of
	   @object, viz. object whose key is stored in dotdot
	   entry. Wouldn't work with hard-links on directories. */
	xmemset(&goodby_dots, 0, sizeof goodby_dots);
	entry.obj = goodby_dots.d_inode = parent;
	goodby_dots.d_name.name = "..";
	goodby_dots.d_name.len = 2;
	result = hashed_rem_entry(object, &goodby_dots, &entry);
	reiser4_free_dentry_fsdata(&goodby_dots);
	if (result != 0)
		warning("nikita-2253", "Cannot remove .. of %lli: %i", 
			get_inode_oid(object), result);

	reiser4_del_nlink(parent, object, 0);
	return 0;
}


/* ->owns_item() for hashed directory object plugin. */
int
hashed_owns_item(const struct inode *inode /* object to check against */ ,
		 const coord_t * coord /* coord of item to check */ )
{
	reiser4_key item_key;

	assert("nikita-1335", inode != NULL);
	assert("nikita-1334", coord != NULL);

	if (item_type_by_coord(coord) == DIR_ENTRY_ITEM_TYPE)
		return get_key_locality(item_key_by_coord(coord, &item_key)) == get_inode_oid(inode);
	else
		return common_file_owns_item(inode, coord);
}

/* helper function for directory_file_create(). Create "." and ".." */
static int
create_dot_dotdot(struct inode *object	/* object to create dot and
					 * dotdot for */ ,
		  struct inode *parent /* parent of @object */ )
{
	int result;
	struct dentry dots_entry;
	reiser4_dir_entry_desc entry;

	assert("nikita-688", object != NULL);
	assert("nikita-689", S_ISDIR(object->i_mode));
	assert("nikita-691", parent != NULL);
	trace_stamp(TRACE_DIR);

	/* We store dot and dotdot as normal directory entries. This is
	   not necessary, because almost all information stored in them
	   is already in the stat-data of directory, the only thing
	   being missed is objectid of grand-parent directory that can
	   easily be added there as extension.
	  
	   But it is done the way it is done, because not storing dot
	   and dotdot will lead to the following complications:
	  
	   . special case handling in ->lookup().
	   . addition of another extension to the sd.
	   . dependency on key allocation policy for stat data.
	  
	*/

	xmemset(&entry, 0, sizeof entry);
	xmemset(&dots_entry, 0, sizeof dots_entry);
	entry.obj = dots_entry.d_inode = object;
	dots_entry.d_name.name = ".";
	dots_entry.d_name.len = 1;
	result = hashed_add_entry(object, &dots_entry, NULL, &entry);

	if (result == 0)
		result = reiser4_add_nlink(object, parent, 0);
	else
		warning("nikita-2222", "Failed to create dot in %llu: %i", get_inode_oid(object), result);

	if (result == 0) {
		entry.obj = dots_entry.d_inode = parent;
		dots_entry.d_name.name = "..";
		dots_entry.d_name.len = 2;
		result = hashed_add_entry(object, &dots_entry, NULL, &entry);
		reiser4_free_dentry_fsdata(&dots_entry);
		/* if creation of ".." failed, iput() will delete object
		   with ".". */
		if (result != 0)
			warning("nikita-2234", "Failed to create dotdot in %llu: %i", get_inode_oid(object), result);
	}

	if (result == 0)
		result = reiser4_add_nlink(parent, object, 0);

	return result;
}

/* implementation of ->resolve_into_inode() method for hashed directories. */
file_lookup_result hashed_lookup(struct inode * parent	/* inode of directory to
							 * lookup into */ ,
				 struct dentry * dentry /* name to look for */ )
{
	int result;
	coord_t *coord;
	lock_handle lh;
	const char *name;
	int len;
	reiser4_dir_entry_desc entry;

	assert("nikita-1247", parent != NULL);
	assert("nikita-1248", dentry != NULL);
	assert("nikita-1123", dentry->d_name.name != NULL);

	if (perm_chk(parent, lookup, parent, dentry))
		return -EPERM;

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	if (!is_name_acceptable(parent, name, len))
		/* some arbitrary error code to return */
		return -ENAMETOOLONG;

	/* set up operations on dentry. */
	dentry->d_op = &reiser4_dentry_operation;

	coord = &reiser4_get_dentry_fsdata(dentry)->dec.entry_coord;
	init_lh(&lh);

	trace_on(TRACE_DIR | TRACE_VFS_OPS, "lookup inode: %lli \"%s\"\n", get_inode_oid(parent), dentry->d_name.name);

	/* find entry in a directory. This is plugin method. */
	result = find_entry(parent, dentry, &lh, ZNODE_READ_LOCK, &entry);
	if (result == 0)
		/* entry was found, extract object key from it. */
		result = WITH_DATA(coord->node, item_plugin_by_coord(coord)->s.dir.extract_key(coord, &entry.key));
	done_lh(&lh);

	if (result == 0) {
		struct inode *inode;

		inode = reiser4_iget(parent->i_sb, &entry.key);
		if (!IS_ERR(inode)) {
			if (inode_get_flag(inode, REISER4_LIGHT_WEIGHT)) {
				inode->i_uid = parent->i_uid;
				inode->i_gid = parent->i_gid;
				/* clear light-weight flag. If inode would be
				   read by any other name, [ug]id wouldn't
				   change. */
				inode_clr_flag(inode, REISER4_LIGHT_WEIGHT);
			}
			/* success */
			d_add(dentry, inode);
			if (inode->i_state & I_NEW)
				unlock_new_inode(inode);
		} else
			result = PTR_ERR(inode);
	}
	return result;
}

static const char *possible_leak = "Possible disk space leak.";

/* re-bind existing name at @from_coord in @from_dir to point to @to_inode.
  
   Helper function called from hashed_rename() */
static int
replace_name(struct inode *to_inode	/* inode where @from_coord is
					 * to be re-targeted at */ ,
	     struct inode *from_dir	/* directory where @from_coord
					 * lives */ ,
	     struct inode *from_inode	/* inode @from_coord
					 * originally point to */ ,
	     coord_t * from_coord	/* where directory entry is in
					 * the tree */ ,
	     lock_handle * from_lh /* lock handle on @from_coord */ )
{
	item_plugin *from_item;
	int result;
	znode *node;

	node = from_coord->node;
	result = zload(node);
	if (result != 0)
		return result;
	from_item = item_plugin_by_coord(from_coord);
	if (item_type_by_coord(from_coord) == DIR_ENTRY_ITEM_TYPE) {
		reiser4_key to_key;

		build_sd_key(to_inode, &to_key);

		/* everything is found and prepared to change directory entry
		   at @from_coord to point to @to_inode.
		  
		   @to_inode is just about to get new name, so bump its link
		   counter.
		  
		*/
		result = reiser4_add_nlink(to_inode, from_dir, 0);
		if (result != 0) {
			/* Don't issue warning: this may be plain -EMLINK */
			zrelse(node);
			return result;
		}

		result = from_item->s.dir.update_key(from_coord, &to_key, from_lh);
		if (result != 0) {
			reiser4_del_nlink(to_inode, from_dir, 0);
			zrelse(node);
			return result;
		}

		/* @from_inode just lost its name, he-he.
		  
		   If @from_inode was directory, it contained dotdot pointing
		   to @from_dir. @from_dir i_nlink will be decreased when
		   iput() will be called on @from_inode.
		  
		   If file-system is not ADG (hard-links are
		   supported on directories), iput(from_inode) will not remove
		   @from_inode, and thus above is incorrect, but hard-links on
		   directories are problematic in many other respects.
		*/
		result = reiser4_del_nlink(from_inode, from_dir, 0);
		if (result != 0) {
			warning("nikita-2330", "Cannot remove link from source: %i. %s", result, possible_leak);
			/* Has to return success, because entry is already
			   modified. */
			result = 0;
		}

		/* NOTE-NIKITA consider calling plugin method in stead of
		   accessing inode fields directly. */
		from_dir->i_mtime = CURRENT_TIME;
	} else {
		warning("nikita-2326", "Unexpected item type");
		print_plugin("item", item_plugin_to_plugin(from_item));
		result = -EIO;
	}
	zrelse(node);
	return result;
}

/* add new entry pointing to @inode into @dir at @coord, locked by @lh
  
   Helper function used by hashed_rename(). */
static int
add_name(struct inode *inode	/* inode where @coord is to be
				 * re-targeted at */ ,
	 struct inode *dir /* directory where @coord lives */ ,
	 struct dentry *name /* new name */ ,
	 coord_t * coord /* where directory entry is in the tree */ ,
	 lock_handle * lh /* lock handle on @coord */ ,
	 int is_dir /* true, if @inode is directory */ )
{
	int result;
	reiser4_dir_entry_desc entry;

	assert("nikita-2333", lh->node == coord->node);
	assert("nikita-2334", is_dir == S_ISDIR(inode->i_mode));

	xmemset(&entry, 0, sizeof entry);
	entry.obj = inode;
	/* build key of directory entry description */
	result = inode_dir_plugin(dir)->entry_key(dir, &name->d_name, &entry.key);
	if (result != 0)
		return result;

	if (is_dir)
		/* ext2 does this in different order: first inserts new entry,
		   then increases directory nlink. We don't want do this,
		   because reiser4_add_nlink() calls ->add_link() plugin
		   method that can fail for whatever reason, leaving as with
		   cleanup problems.
		*/
		result = reiser4_add_nlink(dir, inode, 0);
	if (result == 0) {
		/* @inode is getting new name */
		reiser4_add_nlink(inode, dir, 0);
		/* create @new_name in @new_dir pointing to
		   @old_inode */
		result = WITH_DATA(coord->node,
				   inode_dir_item_plugin(dir)->s.dir.add_entry(dir, coord, lh, name, &entry));
		if (result != 0) {
			result = reiser4_del_nlink(inode, dir, 0);
			if (result != 0) {
				warning("nikita-2327", "Cannot drop link on source: %i. %s", result, possible_leak);
			}
			result = reiser4_del_nlink(dir, inode, 0);
			if (result != 0) {
				warning("nikita-2328", "Cannot drop link on target dir %i. %s", result, possible_leak);
			}
			/* Has to return success, because entry is already
			   created. */
			result = 0;
		} else
			INODE_INC_FIELD(dir, i_size);
	}
	return result;
}

reiser4_block_nr hashed_estimate_rename(
	struct inode  *old_dir  /* directory where @old is located */,
	struct dentry *old_name /* old name */,
	struct inode  *new_dir  /* directory where @new is located */,
	struct dentry *new_name /* new name */) 
{
	reiser4_block_nr res1, res2;
	dir_plugin *p_parent_old, *p_parent_new;
	file_plugin *p_child_old, *p_child_new;
	
	assert("vpf-311", old_dir != NULL);
	assert("vpf-312", new_dir != NULL);
	assert("vpf-313", old_name != NULL);
	assert("vpf-314", new_name != NULL);
	
	p_parent_old = inode_dir_plugin(old_dir);
	p_parent_new = inode_dir_plugin(new_dir);
	p_child_old = inode_file_plugin(old_name->d_inode);
	if (new_name->d_inode)
		p_child_new = inode_file_plugin(new_name->d_inode);
	else
		p_child_new = 0;

	/* find_entry - can insert one leaf. */
	res1 = res2 = 1;
	
	/* replace_name */
	{
		/* reiser4_add_nlink(p_child_old) and reiser4_del_nlink(p_child_old) */
		res1 += 2 * p_child_old->estimate.update(old_name->d_inode);
		/* update key */
		res1 += 1;
		/* reiser4_del_nlink(p_child_new) */
		if (p_child_new)
		    res1 += p_child_new->estimate.update(new_name->d_inode);
	}
    
	/* else add_name */
	{
		/* reiser4_add_nlink(p_parent_new) and reiser4_del_nlink(p_parent_new) */
		res2 += 2 * inode_file_plugin(new_dir)->estimate.update(new_dir);
		/* reiser4_add_nlink(p_parent_old) */
		res2 += p_child_old->estimate.update(old_name->d_inode);
		/* add_entry(p_parent_new) */
		res2 += p_parent_new->estimate.add_entry(new_dir);
		/* reiser4_del_nlink(p_parent_old) */
		res2 += p_child_old->estimate.update(old_name->d_inode);
	}

	res1 = res1 < res2 ? res2 : res1;
	
	
	/* reiser4_write_sd(p_parent_new) */
	res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);

	/* reiser4_write_sd(p_child_new) */
	if (p_child_new)
	    res1 += p_child_new->estimate.update(new_name->d_inode);
	
	/* hashed_rem_entry(p_parent_old) */
	res1 += p_parent_old->estimate.rem_entry(old_dir);
	
	/* reiser4_del_nlink(p_child_old) */
	res1 += p_child_old->estimate.update(old_name->d_inode);
	
	/* replace_name */
	{
	    /* reiser4_add_nlink(p_parent_dir_new) */
	    res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);
	    /* update_key */
	    res1 += 1;
	    /* reiser4_del_nlink(p_parent_new) */
	    res1 += inode_file_plugin(new_dir)->estimate.update(new_dir);
	    /* reiser4_del_nlink(p_parent_old) */
	    res1 += inode_file_plugin(old_dir)->estimate.update(old_dir);
	}
	
	/* reiser4_write_sd(p_parent_old) */
	res1 += inode_file_plugin(old_dir)->estimate.update(old_dir);
	
	/* reiser4_write_sd(p_child_old) */
	res1 += p_child_old->estimate.update(old_name->d_inode);

	return res1;
}

static int hashed_rename_estimate_and_grab(
	struct inode *old_dir /* directory where @old is located */ ,
	struct dentry *old_name /* old name */ ,
	struct inode *new_dir /* directory where @new is located */ ,
	struct dentry *new_name /* new name */ ) 
{
	reiser4_block_nr reserve;
    
	reserve = hashed_estimate_rename(old_dir, old_name, new_dir, new_name);
	
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT, "hashed_rename"))
		return -ENOSPC;

	return 0;
}

/* ->rename directory plugin method implementation for hashed directories. 
  
   See comments in the body.
  
   It is arguable that this function can be made generic so, that it will be
   applicable to any kind of directory plugin that deals with directories
   composed out of directory entries. The only obstacle here is that we don't
   have any data-type to represent directory entry. This should be
   re-considered when more than one different directory plugin will be
   implemented.
*/
int
hashed_rename(struct inode *old_dir /* directory where @old is located */ ,
	      struct dentry *old_name /* old name */ ,
	      struct inode *new_dir /* directory where @new is located */ ,
	      struct dentry *new_name /* new name */ )
{
	/* From `The Open Group Base Specifications Issue 6'
	  
	  
	   If either the old or new argument names a symbolic link, rename()
	   shall operate on the symbolic link itself, and shall not resolve
	   the last component of the argument. If the old argument and the new
	   argument resolve to the same existing file, rename() shall return
	   successfully and perform no other action.
	  
	   [this is done by VFS: vfs_rename()]
	  
	  
	   If the old argument points to the pathname of a file that is not a
	   directory, the new argument shall not point to the pathname of a
	   directory. 
	  
	   [checked by VFS: vfs_rename->may_delete()]
	  
	              If the link named by the new argument exists, it shall
	   be removed and old renamed to new. In this case, a link named new
	   shall remain visible to other processes throughout the renaming
	   operation and refer either to the file referred to by new or old
	   before the operation began. 
	  
	   [we should assure this]
	  
	                               Write access permission is required for
	   both the directory containing old and the directory containing new.
	  
	   [checked by VFS: vfs_rename->may_delete(), may_create()]
	  
	   If the old argument points to the pathname of a directory, the new
	   argument shall not point to the pathname of a file that is not a
	   directory. 
	  
	   [checked by VFS: vfs_rename->may_delete()]
	  
	              If the directory named by the new argument exists, it
	   shall be removed and old renamed to new. In this case, a link named
	   new shall exist throughout the renaming operation and shall refer
	   either to the directory referred to by new or old before the
	   operation began. 
	  
	   [we should assure this]
	  
	                    If new names an existing directory, it shall be
	   required to be an empty directory.
	  
	   [we should check this]
	  
	   If the old argument points to a pathname of a symbolic link, the
	   symbolic link shall be renamed. If the new argument points to a
	   pathname of a symbolic link, the symbolic link shall be removed.
	  
	   The new pathname shall not contain a path prefix that names
	   old. Write access permission is required for the directory
	   containing old and the directory containing new. If the old
	   argument points to the pathname of a directory, write access
	   permission may be required for the directory named by old, and, if
	   it exists, the directory named by new.
	  
	   [checked by VFS: vfs_rename(), vfs_rename_dir()]
	  
	   If the link named by the new argument exists and the file's link
	   count becomes 0 when it is removed and no process has the file
	   open, the space occupied by the file shall be freed and the file
	   shall no longer be accessible. If one or more processes have the
	   file open when the last link is removed, the link shall be removed
	   before rename() returns, but the removal of the file contents shall
	   be postponed until all references to the file are closed.
	  
	   [iput() handles this, but we can do this manually, a la
	   reiser4_unlink()]
	  
	   Upon successful completion, rename() shall mark for update the
	   st_ctime and st_mtime fields of the parent directory of each file.
	  
	   [N/A]
	  
	*/

	int result;
	int is_dir;		/* is @old_name directory */

	struct inode *old_inode;
	struct inode *new_inode;

	reiser4_dir_entry_desc old_entry;
	reiser4_dir_entry_desc new_entry;

	coord_t *old_coord;
	coord_t *new_coord;

	reiser4_dentry_fsdata *old_fsdata;
	reiser4_dentry_fsdata *new_fsdata;

	lock_handle new_lh;

	dir_plugin *dplug;
	int res;

	assert("nikita-2318", old_dir != NULL);
	assert("nikita-2319", new_dir != NULL);
	assert("nikita-2320", old_name != NULL);
	assert("nikita-2321", new_name != NULL);

	old_inode = old_name->d_inode;
	new_inode = new_name->d_inode;

	dplug = inode_dir_plugin(old_dir);

	old_fsdata = reiser4_get_dentry_fsdata(old_name);
	new_fsdata = reiser4_get_dentry_fsdata(new_name);

	old_coord = &old_fsdata->dec.entry_coord;
	new_coord = &new_fsdata->dec.entry_coord;

	is_dir = S_ISDIR(old_inode->i_mode);

	/* if target is existing directory and it's not empty---return error.
	  
	   This check is done specifically, because is_dir_empty() requires
	   tree traversal and have to be done before an locks are taken.
	*/
	if (is_dir && (new_inode != NULL) && (is_dir_empty(new_inode) != 0))
		return -ENOTEMPTY;

	res = hashed_rename_estimate_and_grab(old_dir, old_name, new_dir, new_name);
	if (res)
	    return res;
		
	init_lh(&new_lh);

	/* find entry for @new_name */
	result = find_entry(new_dir, new_name, &new_lh, ZNODE_WRITE_LOCK, &new_entry);

	if ((result != CBK_COORD_FOUND) && (result != CBK_COORD_NOTFOUND)) {
		done_lh(&new_lh);
		return result;
	}

	seal_done(&new_fsdata->dec.entry_seal);

	/* add or replace name for @old_inode as @new_name */
	if (new_inode != NULL) {
		/* target (@new_name) exists. */
		/* Not clear what to do with objects that are
		   both directories and files at the same time. */
		if (result == CBK_COORD_FOUND)
			result = replace_name(old_inode, new_dir, new_inode, new_coord, &new_lh);
		else if (result == CBK_COORD_NOTFOUND) {
			/* VFS told us that @new_name is bound to existing
			   inode, but we failed to find directory entry. */
			warning("nikita-2324", "Target not found");
			result = -ENOENT;
		}
	} else {
		/* target (@new_name) doesn't exists. */
		if (result == CBK_COORD_NOTFOUND)
			result = add_name(old_inode, new_dir, new_name, new_coord, &new_lh, is_dir);
		else if (result == CBK_COORD_FOUND) {
			/* VFS told us that @new_name is "negative" dentry,
			   but we found directory entry. */
			warning("nikita-2331", "Target found unexpectedly");
			result = -EIO;
		}
	}

	/* We are done with all modifications to the @new_dir, release lock on
	   node. */
	done_lh(&new_lh);
	reiser4_write_sd(new_dir);
	if (new_inode != NULL)
		reiser4_write_sd(new_inode);

	if (result != 0)
		return result;

	xmemset(&old_entry, 0, sizeof old_entry);
	old_entry.obj = old_inode;
	result = dplug->entry_key(old_dir, &old_name->d_name, &old_entry.key);
	if (result != 0)
		return result;

	/* At this stage new name was introduced for @old_inode. @old_inode,
	   @new_dir, and @new_inode i_nlink counters were updated.
	  
	   We want to remove @old_name now. If @old_inode wasn't directory
	   this is simple.
	*/
	result = hashed_rem_entry(old_dir, old_name, &old_entry);
	if (result != 0) {
		warning("nikita-2335", "Cannot remove old name: %i", result);
	} else {
		result = reiser4_del_nlink(old_inode, old_dir, 0);
		if (result != 0) {
			warning("nikita-2337", "Cannot drop link on old: %i", result);
		}
	}

	if ((result == 0) && is_dir) {
		/* @old_inode is directory. We also have to update dotdot
		   entry. */
		coord_t *dotdot_coord;
		lock_handle dotdot_lh;
		struct dentry dotdot_name;
		reiser4_dir_entry_desc dotdot_entry;

		xmemset(&dotdot_entry, 0, sizeof dotdot_entry);
		dotdot_entry.obj = old_dir;
		xmemset(&dotdot_name, 0, sizeof dotdot_name);
		dotdot_name.d_name.name = "..";
		dotdot_name.d_name.len = 2;

		init_lh(&dotdot_lh);

		dotdot_coord = &reiser4_get_dentry_fsdata(&dotdot_name)->dec.entry_coord;

		result = find_entry(old_inode, &dotdot_name, &dotdot_lh, ZNODE_WRITE_LOCK, &dotdot_entry);
		if (result == 0) {
			result = replace_name(new_dir, old_inode, old_dir, dotdot_coord, &dotdot_lh);
			/* replace_name() decreases i_nlink on @old_dir */
		} else {
			warning("nikita-2336", "Dotdot not found in %llu", get_inode_oid(old_inode));
			result = -EIO;
		}
		done_lh(&dotdot_lh);
		reiser4_free_dentry_fsdata(&dotdot_name);
	}
	reiser4_write_sd(old_dir);
	reiser4_write_sd(old_inode);
	return result;
}

/* ->add_entry() method for hashed directory object plugin. */
int
hashed_add_entry(struct inode *object	/* directory to add new name
					 * in */ ,
		 struct dentry *where /* new name */ ,
		 reiser4_object_create_data * data UNUSED_ARG	/* parameters
								 * of new
								 * object */ ,
		 reiser4_dir_entry_desc * entry	/* parameters of new
						 * directory entry */ )
{
	int result;
	coord_t *coord;
	lock_handle lh;
	reiser4_dentry_fsdata *fsdata;
	reiser4_block_nr       reserve;

	assert("nikita-1114", object != NULL);
	assert("nikita-1250", where != NULL);

	fsdata = reiser4_get_dentry_fsdata(where);
	if (unlikely(IS_ERR(fsdata)))
		return PTR_ERR(fsdata);
		
	reserve = inode_dir_plugin(object)->estimate.add_entry(object);
	if (reiser4_grab_space(reserve, BA_CAN_COMMIT, "hashed_add_entry"))
		return -ENOSPC;

	init_lh(&lh);
	trace_on(TRACE_DIR, "[%i]: creating \"%s\" in %llu\n", current->pid, where->d_name.name, get_inode_oid(object));
	coord = &fsdata->dec.entry_coord;

	/* check for this entry in a directory. This is plugin method. */
	result = find_entry(object, where, &lh, ZNODE_WRITE_LOCK, entry);
	if (likely(result == -ENOENT)) {
		/* add new entry. Just pass control to the directory
		   item plugin. */
		assert("nikita-1709", inode_dir_item_plugin(object));
		assert("nikita-2230", coord->node == lh.node);
		seal_done(&fsdata->dec.entry_seal);
		result = inode_dir_item_plugin(object)->s.dir.add_entry(object, coord, &lh, where, entry);
		if (result == 0) {
			adjust_dir_file(object, where, fsdata->dec.pos + 1, +1);
			INODE_INC_FIELD(object, i_size);
		}
	} else if (result == 0) {
		assert("nikita-2232", coord->node == lh.node);
		result = -EEXIST;
	}
	done_lh(&lh);

	return result;
}

/* ->rem_entry() method for hashed directory object plugin. */
int
hashed_rem_entry(struct inode *object	/* directory from which entry
					 * is begin removed */ ,
		 struct dentry *where	/* name that is being
					 * removed */ ,
		 reiser4_dir_entry_desc * entry	/* description of entry being
						 * removed */ )
{
	int result;
	coord_t *coord;
	lock_handle lh;
	reiser4_dentry_fsdata *fsdata;
	assert("nikita-1124", object != NULL);
	assert("nikita-1125", where != NULL);

	if (reiser4_grab_space(inode_dir_plugin(object)->estimate.rem_entry(object), BA_CAN_COMMIT | BA_RESERVED, __FUNCTION__))
		return -ENOSPC;
	
	init_lh(&lh);

	/* check for this entry in a directory. This is plugin method. */
	result = find_entry(object, where, &lh, ZNODE_WRITE_LOCK, entry);
	fsdata = reiser4_get_dentry_fsdata(where);
	coord = &fsdata->dec.entry_coord;
	if (result == 0) {
		/* remove entry. Just pass control to the directory item
		   plugin. */
		assert("vs-542", inode_dir_item_plugin(object));
		seal_done(&fsdata->dec.entry_seal);
		adjust_dir_file(object, where, fsdata->dec.pos, -1);
		result = WITH_DATA(coord->node,
				   inode_dir_item_plugin(object)->s.dir.rem_entry(object, &where->d_name, coord, &lh, entry));
		if (result == 0) {
			if (object->i_size >= 1)
				INODE_DEC_FIELD(object, i_size);
			else {
				warning("nikita-2509", "Dir %llu is runt", get_inode_oid(object));
				result = -EIO;
			}
		}
	}
	done_lh(&lh);

	return result;
}

static int entry_actor(reiser4_tree * tree /* tree being scanned */ ,
		       coord_t * coord /* current coord */ ,
		       lock_handle * lh /* current lock handle */ ,
		       void *args /* argument to scan */ );

typedef struct entry_actor_args {
	const char *name;
	reiser4_key *key;
	int non_uniq;
#if REISER4_USE_COLLISION_LIMIT || REISER4_STATS
	int max_non_uniq;
#endif
	int not_found;
	znode_lock_mode mode;

	coord_t last_coord;
	lock_handle last_lh;
	const struct inode *inode;
} entry_actor_args;

static int
check_entry(const struct inode *dir, coord_t *coord, const struct qstr *name)
{
	return WITH_DATA(coord->node, check_item(dir, coord, name->name));
}

/* Look for given @name within directory @dir.
  
   This is called during lookup, creation and removal of directory
   entries.
  
   First calculate key that directory entry for @name would have. Search
   for this key in the tree. If such key is found, scan all items with
   the same key, checking name in each directory entry along the way.
  
*/
static int
find_entry(const struct inode *dir /* directory to scan */,
	   struct dentry *de /* name to search for */,
	   lock_handle * lh /* resulting lock handle */,
	   znode_lock_mode mode /* required lock mode */,
	   reiser4_dir_entry_desc * entry /* parameters of found directory
					   * entry */)
{
	const struct qstr *name;
	seal_t *seal;
	coord_t *coord;
	int result;
	__u32 flags;
	de_control *dec;

	assert("nikita-1130", lh != NULL);
	assert("nikita-1128", dir != NULL);

	name = &de->d_name;
	assert("nikita-1129", name != NULL);

	/* dentry private data don't require lock, because dentry
	   manipulations are protected by i_sem on parent.  
	  
	   This is not so for inodes, because there is no -the- parent in
	   inode case.
	*/
	dec = &reiser4_get_dentry_fsdata(de)->dec;
	coord = &dec->entry_coord;
	seal = &dec->entry_seal;
	/* compose key of directory entry for @name */
	result = inode_dir_plugin(dir)->entry_key(dir, name, &entry->key);
	if (result != 0)
		return result;

	if (seal_is_set(seal)) {
		/* check seal */
		result = seal_validate(seal, coord, &entry->key, LEAF_LEVEL, 
				       lh, FIND_EXACT, mode, ZNODE_LOCK_LOPRI);
		if (result == 0) {
			/* key was found. Check that it is really item we are
			   looking for. */
			result = check_entry(dir, coord, name);
			if (result == 0)
				return 0;
		}
	}
	flags = (mode == ZNODE_WRITE_LOCK) ? CBK_FOR_INSERT : 0;
	result = coord_by_key(tree_by_inode(dir), &entry->key, coord, lh,
			      mode, FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, flags, 0/*ra_info*/);

	if (result == CBK_COORD_FOUND) {
		entry_actor_args arg;

		/* fast path: no hash collisions */
		result = check_entry(dir, coord, name);
		if (result == 0) {
			seal_init(seal, coord, &entry->key);
			dec->pos = 0;
		} else if (result > 0) {
			/* Iterate through all units with the same keys. */
			arg.name = name->name;
			arg.key = &entry->key;
			arg.not_found = 0;
			arg.non_uniq = 0;
#if REISER4_USE_COLLISION_LIMIT
			arg.max_non_uniq = max_hash_collisions(dir);
			assert("nikita-2851", arg.max_non_uniq > 1);
#endif
			arg.mode = mode;
			arg.inode = dir;
			coord_init_zero(&arg.last_coord);
			init_lh(&arg.last_lh);

			result = iterate_tree(tree_by_inode(dir), coord, lh, 
					      entry_actor, &arg, mode, 1);
			/* if end of the tree or extent was reached during
			   scanning. */
			if (arg.not_found || (result == -E_NO_NEIGHBOR)) {
				/* step back */
				done_lh(lh);

				result = zload(arg.last_coord.node);
				if (result == 0) {
					coord_dup(coord, &arg.last_coord);
					move_lh(lh, &arg.last_lh);
					result = -ENOENT;
					zrelse(arg.last_coord.node);
					--arg.non_uniq;
				}
			}

			done_lh(&arg.last_lh);
			if (result == 0)
				seal_init(seal, coord, &entry->key);

			if (result == 0 || result == -ENOENT) {
				assert("nikita-2580", arg.non_uniq > 0);
				dec->pos = arg.non_uniq - 1;
			}
		}
	} else
		dec->pos = -1;
	return result;
}

/* Function called by find_entry() to look for given name in the directory. */
static int
entry_actor(reiser4_tree * tree UNUSED_ARG /* tree being scanned */ ,
	    coord_t * coord /* current coord */ ,
	    lock_handle * lh /* current lock handle */ ,
	    void *entry_actor_arg /* argument to scan */ )
{
	reiser4_key unit_key;
	entry_actor_args *args;

	assert("nikita-1131", tree != NULL);
	assert("nikita-1132", coord != NULL);
	assert("nikita-1133", entry_actor_arg != NULL);

	args = entry_actor_arg;
	++args->non_uniq;
#if REISER4_USE_COLLISION_LIMIT
	reiser4_stat_nuniq_max((unsigned) args->non_uniq);
	if (args->non_uniq > args->max_non_uniq) {
		args->not_found = 1;
		/* hash collision overflow. */
		return -EBUSY;
	}
#endif

	if (!keyeq(args->key, unit_key_by_coord(coord, &unit_key))) {
		assert("nikita-1791", keylt(args->key, unit_key_by_coord(coord, &unit_key)));
		args->not_found = 1;
		args->last_coord.between = AFTER_UNIT;
		return 0;
	}

	coord_dup(&args->last_coord, coord);
	if (args->last_lh.node != lh->node) {
		int lock_result;

		done_lh(&args->last_lh);
		assert("nikita-1896", znode_is_any_locked(lh->node));
		lock_result = longterm_lock_znode(&args->last_lh, lh->node, args->mode, ZNODE_LOCK_HIPRI);
		if (lock_result != 0)
			return lock_result;
	}
	return check_item(args->inode, coord, args->name);
}

static int
check_item(const struct inode *dir, const coord_t * coord, const char *name)
{
	item_plugin *iplug;
	char buf[DE_NAME_BUF_LEN];

	iplug = item_plugin_by_coord(coord);
	if (iplug == NULL) {
		warning("nikita-1135", "Cannot get item plugin");
		print_coord("coord", coord, 1);
		return -EIO;
	} else if (item_id_by_coord(coord) != item_id_by_plugin(inode_dir_item_plugin(dir))) {
		/* item id of current item does not match to id of items a
		   directory is built of */
		warning("nikita-1136", "Wrong item plugin");
		print_coord("coord", coord, 1);
		print_plugin("plugin", item_plugin_to_plugin(iplug));
		return -EIO;
	}
	assert("nikita-1137", iplug->s.dir.extract_name);

	trace_on(TRACE_DIR, "[%i]: check_item: \"%s\", \"%s\" in %lli (%lli)\n",
		 current->pid, name, iplug->s.dir.extract_name(coord, buf),
		 get_inode_oid(dir), *znode_get_block(coord->node));
	/* Compare name stored in this entry with name we are looking for.
	   
	   NOTE-NIKITA Here should go code for support of something like
	   unicode, code tables, etc.
	*/
	return !!strcmp(name, iplug->s.dir.extract_name(coord, buf));
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
