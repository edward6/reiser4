/* Copyright 2005 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "../inode.h"

/* return true, iff @coord points to the valid directory item that is part of
 * @inode directory. */
static int is_valid_dir_coord(struct inode *inode, coord_t * coord)
{
	return
	    item_type_by_coord(coord) == DIR_ENTRY_ITEM_TYPE &&
	    inode_file_plugin(inode)->owns_item(inode, coord);
}

/* compare two logical positions within the same directory */
static cmp_t dir_pos_cmp(const dir_pos * p1, const dir_pos * p2)
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

/* true, if file descriptor @f is created by NFS server by "demand" to serve
 * one file system operation. This means that there may be "detached state"
 * for underlying inode. */
static inline int file_is_stateless(struct file *f)
{
	return reiser4_get_dentry_fsdata(f->f_dentry)->stateless;
}

#define CID_SHIFT (20)
#define CID_MASK  (0xfffffull)

/* calculate ->fpos from user-supplied cookie. Normally it is dir->f_pos, but
 * in the case of stateless directory operation (readdir-over-nfs), client id
 * was encoded in the high bits of cookie and should me masked off. */
static loff_t get_dir_fpos(struct file *dir)
{
	if (file_is_stateless(dir))
		return dir->f_pos & CID_MASK;
	else
		return dir->f_pos;
}

/* see comment before readdir_common() for overview of why "adjustment" is
 * necessary. */
static void
adjust_dir_pos(struct file *dir,
	       readdir_pos * readdir_spot, const dir_pos * mod_point, int adj)
{
	dir_pos *pos;

	/*
	 * new directory entry was added (adj == +1) or removed (adj == -1) at
	 * the @mod_point. Directory file descriptor @dir is doing readdir and
	 * is currently positioned at @readdir_spot. Latter has to be updated
	 * to maintain stable readdir.
	 */
	/* directory is positioned to the beginning. */
	if (readdir_spot->entry_no == 0)
		return;

	pos = &readdir_spot->position;
	switch (dir_pos_cmp(mod_point, pos)) {
	case LESS_THAN:
		/* @mod_pos is _before_ @readdir_spot, that is, entry was
		 * added/removed on the left (in key order) of current
		 * position. */
		/* logical number of directory entry readdir is "looking" at
		 * changes */
		readdir_spot->entry_no += adj;
		assert("nikita-2577",
		       ergo(dir != NULL, get_dir_fpos(dir) + adj >= 0));
		if (de_id_cmp(&pos->dir_entry_key,
			      &mod_point->dir_entry_key) == EQUAL_TO) {
			assert("nikita-2575", mod_point->pos < pos->pos);
			/*
			 * if entry added/removed has the same key as current
			 * for readdir, update counter of duplicate keys in
			 * @readdir_spot.
			 */
			pos->pos += adj;
		}
		break;
	case GREATER_THAN:
		/* directory is modified after @pos: nothing to do. */
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
		memset(readdir_spot, 0, sizeof *readdir_spot);
	}
}

/* scan all file-descriptors for this directory and adjust their
   positions respectively. Should be used by implementations of
   add_entry and rem_entry of dir plugin */
void
adjust_dir_file(struct inode *dir, const struct dentry *de, int offset, int adj)
{
	reiser4_file_fsdata *scan;
	dir_pos mod_point;

	assert("nikita-2536", dir != NULL);
	assert("nikita-2538", de != NULL);
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

/*
 * traverse tree to start/continue readdir from the readdir position @pos.
 */
static int dir_go_to(struct file *dir, readdir_pos * pos, tap_t * tap)
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
			       LEAF_LEVEL, LEAF_LEVEL, 0, &tap->ra_info);
	if (result == CBK_COORD_FOUND)
		result = rewind_right(tap, (int)pos->position.pos);
	else {
		tap->coord->node = NULL;
		done_lh(tap->lh);
		result = RETERR(-EIO);
	}
	return result;
}

/*
 * handling of non-unique keys: calculate at what ordinal position within
 * sequence of directory items with identical keys @pos is.
 */
static int set_pos(struct inode *inode, readdir_pos * pos, tap_t * tap)
{
	int result;
	coord_t coord;
	lock_handle lh;
	tap_t scan;
	de_id *did;
	reiser4_key de_key;

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
			pos->position.pos++;
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
static int dir_rewind(struct file *dir, readdir_pos * pos, tap_t * tap)
{
	__u64 destination;
	__s64 shift;
	int result;
	struct inode *inode;
	loff_t dirpos;

	assert("nikita-2553", dir != NULL);
	assert("nikita-2548", pos != NULL);
	assert("nikita-2551", tap->coord != NULL);
	assert("nikita-2552", tap->lh != NULL);

	dirpos = get_dir_fpos(dir);
	shift = dirpos - pos->fpos;
	/* this is logical directory entry within @dir which we are rewinding
	 * to */
	destination = pos->entry_no + shift;

	inode = dir->f_dentry->d_inode;
	if (dirpos < 0)
		return RETERR(-EINVAL);
	else if (destination == 0ll || dirpos == 0) {
		/* rewind to the beginning of directory */
		memset(pos, 0, sizeof *pos);
		return dir_go_to(dir, pos, tap);
	} else if (destination >= inode->i_size)
		return RETERR(-ENOENT);

	if (shift < 0) {
		/* I am afraid of negative numbers */
		shift = -shift;
		/* rewinding to the left */
		if (shift <= (int)pos->position.pos) {
			/* destination is within sequence of entries with
			   duplicate keys. */
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
						continue;
					}
				}
				break;
			}
		}
	} else {
		/* rewinding to the right */
		result = dir_go_to(dir, pos, tap);
		if (result == 0)
			result = rewind_right(tap, shift);
	}
	if (result == 0) {
		result = set_pos(inode, pos, tap);
		if (result == 0) {
			/* update pos->position.pos */
			pos->entry_no = destination;
			pos->fpos = dirpos;
		}
	}
	return result;
}

/*
 * Function that is called by common_readdir() on each directory entry while
 * doing readdir. ->filldir callback may block, so we had to release long term
 * lock while calling it. To avoid repeating tree traversal, seal is used. If
 * seal is broken, we return -E_REPEAT. Node is unlocked in this case.
 *
 * Whether node is unlocked in case of any other error is undefined. It is
 * guaranteed to be still locked if success (0) is returned.
 *
 * When ->filldir() wants no more, feed_entry() returns 1, and node is
 * unlocked.
 */
static int
feed_entry(struct file *f,
	   readdir_pos * pos, tap_t * tap, filldir_t filldir, void *dirent)
{
	item_plugin *iplug;
	char *name;
	reiser4_key sd_key;
	int result;
	char buf[DE_NAME_BUF_LEN];
	char name_buf[32];
	char *local_name;
	unsigned file_type;
	seal_t seal;
	coord_t *coord;
	reiser4_key entry_key;

	coord = tap->coord;
	iplug = item_plugin_by_coord(coord);

	/* pointer to name within the node */
	name = iplug->s.dir.extract_name(coord, buf);
	assert("nikita-1371", name != NULL);

	/* key of object the entry points to */
	if (iplug->s.dir.extract_key(coord, &sd_key) != 0)
		return RETERR(-EIO);

	/* we must release longterm znode lock before calling filldir to avoid
	   deadlock which may happen if filldir causes page fault. So, copy
	   name to intermediate buffer */
	if (strlen(name) + 1 > sizeof(name_buf)) {
		local_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
		if (local_name == NULL)
			return RETERR(-ENOMEM);
	} else
		local_name = name_buf;

	strcpy(local_name, name);
	file_type = iplug->s.dir.extract_file_type(coord);

	unit_key_by_coord(coord, &entry_key);
	seal_init(&seal, coord, &entry_key);

	longterm_unlock_znode(tap->lh);

	/*
	 * send information about directory entry to the ->filldir() filler
	 * supplied to us by caller (VFS).
	 *
	 * ->filldir is entitled to do weird things. For example, ->filldir
	 * supplied by knfsd re-enters file system. Make sure no locks are
	 * held.
	 */
	assert("nikita-3436", lock_stack_isclean(get_current_lock_stack()));

	result = filldir(dirent, name, (int)strlen(name),
			 /* offset of this entry */
			 f->f_pos,
			 /* inode number of object bounden by this entry */
			 oid_to_uino(get_key_objectid(&sd_key)), file_type);
	if (local_name != name_buf)
		kfree(local_name);
	if (result < 0)
		/* ->filldir() is satisfied. (no space in buffer, IOW) */
		result = 1;
	else
		result = seal_validate(&seal, coord, &entry_key,
				       tap->lh, tap->mode, ZNODE_LOCK_HIPRI);
	return result;
}

static void move_entry(readdir_pos * pos, coord_t * coord)
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
	++pos->fpos;
}

/*
 *     STATELESS READDIR
 *
 * readdir support in reiser4 relies on ability to update readdir_pos embedded
 * into reiser4_file_fsdata on each directory modification (name insertion and
 * removal), see readdir_common() function below. This obviously doesn't work
 * when reiser4 is accessed over NFS, because NFS doesn't keep any state
 * across client READDIR requests for the same directory.
 *
 * To address this we maintain a "pool" of detached reiser4_file_fsdata
 * (d_cursor). Whenever NFS readdir request comes, we detect this, and try to
 * find detached reiser4_file_fsdata corresponding to previous readdir
 * request. In other words, additional state is maintained on the
 * server. (This is somewhat contrary to the design goals of NFS protocol.)
 *
 * To efficiently detect when our ->readdir() method is called by NFS server,
 * dentry is marked as "stateless" in reiser4_decode_fh() (this is checked by
 * file_is_stateless() function).
 *
 * To find out d_cursor in the pool, we encode client id (cid) in the highest
 * bits of NFS readdir cookie: when first readdir request comes to the given
 * directory from the given client, cookie is set to 0. This situation is
 * detected, global cid_counter is incremented, and stored in highest bits of
 * all direntry offsets returned to the client, including last one. As the
 * only valid readdir cookie is one obtained as direntry->offset, we are
 * guaranteed that next readdir request (continuing current one) will have
 * current cid in the highest bits of starting readdir cookie. All d_cursors
 * are hashed into per-super-block hash table by (oid, cid) key.
 *
 * In addition d_cursors are placed into per-super-block radix tree where they
 * are keyed by oid alone. This is necessary to efficiently remove them during
 * rmdir.
 *
 * At last, currently unused d_cursors are linked into special list. This list
 * is used d_cursor_shrink to reclaim d_cursors on memory pressure.
 *
 */

TYPE_SAFE_LIST_DECLARE(d_cursor);
TYPE_SAFE_LIST_DECLARE(a_cursor);

typedef struct {
	__u16 cid;
	__u64 oid;
} d_cursor_key;

struct dir_cursor {
	int ref;
	reiser4_file_fsdata *fsdata;
	d_cursor_hash_link hash;
	d_cursor_list_link list;
	d_cursor_key key;
	d_cursor_info *info;
	a_cursor_list_link alist;
};

static kmem_cache_t *d_cursor_slab;
static struct shrinker *d_cursor_shrinker;
static unsigned long d_cursor_unused = 0;
static spinlock_t d_lock = SPIN_LOCK_UNLOCKED;
static a_cursor_list_head cursor_cache = TYPE_SAFE_LIST_HEAD_INIT(cursor_cache);

#define D_CURSOR_TABLE_SIZE (256)

static inline unsigned long
d_cursor_hash(d_cursor_hash_table * table, const d_cursor_key * key)
{
	assert("nikita-3555", IS_POW(D_CURSOR_TABLE_SIZE));
	return (key->oid + key->cid) & (D_CURSOR_TABLE_SIZE - 1);
}

static inline int d_cursor_eq(const d_cursor_key * k1, const d_cursor_key * k2)
{
	return k1->cid == k2->cid && k1->oid == k2->oid;
}

#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TYPE_SAFE_HASH_DEFINE(d_cursor,
		      dir_cursor,
		      d_cursor_key, key, hash, d_cursor_hash, d_cursor_eq);
#undef KFREE
#undef KMALLOC

TYPE_SAFE_LIST_DEFINE(d_cursor, dir_cursor, list);
TYPE_SAFE_LIST_DEFINE(a_cursor, dir_cursor, alist);

static void kill_cursor(dir_cursor * cursor);

/*
 * shrink d_cursors cache. Scan LRU list of unused cursors, freeing requested
 * number. Return number of still freeable cursors.
 */
static int d_cursor_shrink(int nr, unsigned int gfp_mask)
{
	if (nr != 0) {
		dir_cursor *scan;
		int killed;

		killed = 0;
		spin_lock(&d_lock);
		while (!a_cursor_list_empty(&cursor_cache)) {
			scan = a_cursor_list_front(&cursor_cache);
			assert("nikita-3567", scan->ref == 0);
			kill_cursor(scan);
			++killed;
			--nr;
			if (nr == 0)
				break;
		}
		spin_unlock(&d_lock);
	}
	return d_cursor_unused;
}

/*
 * perform global initializations for the d_cursor sub-system.
 */
int d_cursor_init(void)
{
	d_cursor_slab = kmem_cache_create("d_cursor", sizeof(dir_cursor), 0,
					  SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (d_cursor_slab == NULL)
		return RETERR(-ENOMEM);
	else {
		/* actually, d_cursors are "priceless", because there is no
		 * way to recover information stored in them. On the other
		 * hand, we don't want to consume all kernel memory by
		 * them. As a compromise, just assign higher "seeks" value to
		 * d_cursor cache, so that it will be shrunk only if system is
		 * really tight on memory. */
		d_cursor_shrinker = set_shrinker(DEFAULT_SEEKS << 3,
						 d_cursor_shrink);
		if (d_cursor_shrinker == NULL)
			return RETERR(-ENOMEM);
		else
			return 0;
	}
}

/*
 * Dual to d_cursor_init(): release global d_cursor resources.
 */
void d_cursor_done(void)
{
	if (d_cursor_shrinker != NULL) {
		remove_shrinker(d_cursor_shrinker);
		d_cursor_shrinker = NULL;
	}
	if (d_cursor_slab != NULL) {
		kmem_cache_destroy(d_cursor_slab);
		d_cursor_slab = NULL;
	}
}

/*
 * initialize per-super-block d_cursor resources
 */
int d_cursor_init_at(struct super_block *s)
{
	d_cursor_info *p;

	p = &get_super_private(s)->d_info;

	INIT_RADIX_TREE(&p->tree, GFP_KERNEL);
	return d_cursor_hash_init(&p->table, D_CURSOR_TABLE_SIZE);
}

/*
 * Dual to d_cursor_init_at: release per-super-block d_cursor resources
 */
void d_cursor_done_at(struct super_block *s)
{
	d_cursor_hash_done(&get_super_private(s)->d_info.table);
}

/*
 * return d_cursor data for the file system @inode is in.
 */
static inline d_cursor_info *d_info(struct inode *inode)
{
	return &get_super_private(inode->i_sb)->d_info;
}

/*
 * lookup d_cursor in the per-super-block radix tree.
 */
static inline dir_cursor *lookup(d_cursor_info * info, unsigned long index)
{
	return (dir_cursor *) radix_tree_lookup(&info->tree, index);
}

/*
 * attach @cursor to the radix tree. There may be multiple cursors for the
 * same oid, they are chained into circular list.
 */
static void bind_cursor(dir_cursor * cursor, unsigned long index)
{
	dir_cursor *head;

	head = lookup(cursor->info, index);
	if (head == NULL) {
		/* this is the first cursor for this index */
		d_cursor_list_clean(cursor);
		radix_tree_insert(&cursor->info->tree, index, cursor);
	} else {
		/* some cursor already exists. Chain ours */
		d_cursor_list_insert_after(head, cursor);
	}
}

/*
 * remove @cursor from indices and free it
 */
static void kill_cursor(dir_cursor * cursor)
{
	unsigned long index;

	assert("nikita-3566", cursor->ref == 0);
	assert("nikita-3572", cursor->fsdata != NULL);

	index = (unsigned long)cursor->key.oid;
	readdir_list_remove_clean(cursor->fsdata);
	reiser4_free_fsdata(cursor->fsdata);
	cursor->fsdata = NULL;

	if (d_cursor_list_is_clean(cursor))
		/* this is last cursor for a file. Kill radix-tree entry */
		radix_tree_delete(&cursor->info->tree, index);
	else {
		void **slot;

		/*
		 * there are other cursors for the same oid.
		 */

		/*
		 * if radix tree point to the cursor being removed, re-target
		 * radix tree slot to the next cursor in the (non-empty as was
		 * checked above) element of the circular list of all cursors
		 * for this oid.
		 */
		slot = radix_tree_lookup_slot(&cursor->info->tree, index);
		assert("nikita-3571", *slot != NULL);
		if (*slot == cursor)
			*slot = d_cursor_list_next(cursor);
		/* remove cursor from circular list */
		d_cursor_list_remove_clean(cursor);
	}
	/* remove cursor from the list of unused cursors */
	a_cursor_list_remove_clean(cursor);
	/* remove cursor from the hash table */
	d_cursor_hash_remove(&cursor->info->table, cursor);
	/* and free it */
	kmem_cache_free(d_cursor_slab, cursor);
	--d_cursor_unused;
}

/* possible actions that can be performed on all cursors for the given file */
enum cursor_action {
	/* load all detached state: this is called when stat-data is loaded
	 * from the disk to recover information about all pending readdirs */
	CURSOR_LOAD,
	/* detach all state from inode, leaving it in the cache. This is
	 * called when inode is removed form the memory by memory pressure */
	CURSOR_DISPOSE,
	/* detach cursors from the inode, and free them. This is called when
	 * inode is destroyed. */
	CURSOR_KILL
};

static void process_cursors(struct inode *inode, enum cursor_action act)
{
	oid_t oid;
	dir_cursor *start;
	readdir_list_head *head;
	reiser4_context *ctx;
	d_cursor_info *info;

	/* this can be called by
	 *
	 * kswapd->...->prune_icache->..reiser4_destroy_inode
	 *
	 * without reiser4_context
	 */
	ctx = init_context(inode->i_sb);
	if (IS_ERR(ctx)) {
		warning("vs-23", "failed to init context");
		return;
	}

	assert("nikita-3558", inode != NULL);

	info = d_info(inode);
	oid = get_inode_oid(inode);
	spin_lock_inode(inode);
	head = get_readdir_list(inode);
	spin_lock(&d_lock);
	/* find any cursor for this oid: reference to it is hanging of radix
	 * tree */
	start = lookup(info, (unsigned long)oid);
	if (start != NULL) {
		dir_cursor *scan;
		reiser4_file_fsdata *fsdata;

		/* process circular list of cursors for this oid */
		scan = start;
		do {
			dir_cursor *next;

			next = d_cursor_list_next(scan);
			fsdata = scan->fsdata;
			assert("nikita-3557", fsdata != NULL);
			if (scan->key.oid == oid) {
				switch (act) {
				case CURSOR_DISPOSE:
					readdir_list_remove_clean(fsdata);
					break;
				case CURSOR_LOAD:
					readdir_list_push_front(head, fsdata);
					break;
				case CURSOR_KILL:
					kill_cursor(scan);
					break;
				}
			}
			if (scan == next)
				/* last cursor was just killed */
				break;
			scan = next;
		} while (scan != start);
	}
	spin_unlock(&d_lock);
	/* check that we killed 'em all */
	assert("nikita-3568", ergo(act == CURSOR_KILL,
				   readdir_list_empty(get_readdir_list
						      (inode))));
	assert("nikita-3569",
	       ergo(act == CURSOR_KILL, lookup(info, oid) == NULL));
	spin_unlock_inode(inode);
	reiser4_exit_context(ctx);
}

/* detach all cursors from inode. This is called when inode is removed from
 * the memory by memory pressure */
void dispose_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_DISPOSE);
}

/* attach all detached cursors to the inode. This is done when inode is loaded
 * into memory */
void load_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_LOAD);
}

/* free all cursors for this inode. This is called when inode is destroyed. */
void kill_cursors(struct inode *inode)
{
	process_cursors(inode, CURSOR_KILL);
}

/* global counter used to generate "client ids". These ids are encoded into
 * high bits of fpos. */
static __u32 cid_counter = 0;

/*
 * detach fsdata (if detachable) from file descriptor, and put cursor on the
 * "unused" list. Called when file descriptor is not longer in active use.
 */
static void clean_fsdata(struct file *f)
{
	dir_cursor *cursor;
	reiser4_file_fsdata *fsdata;

	assert("nikita-3570", file_is_stateless(f));

	fsdata = (reiser4_file_fsdata *) f->private_data;
	if (fsdata != NULL) {
		cursor = fsdata->cursor;
		if (cursor != NULL) {
			spin_lock(&d_lock);
			--cursor->ref;
			if (cursor->ref == 0) {
				a_cursor_list_push_back(&cursor_cache, cursor);
				++d_cursor_unused;
			}
			spin_unlock(&d_lock);
			f->private_data = NULL;
		}
	}
}

/* add detachable readdir state to the @f */
static int
insert_cursor(dir_cursor * cursor, struct file *f, struct inode *inode)
{
	int result;
	reiser4_file_fsdata *fsdata;

	memset(cursor, 0, sizeof *cursor);

	/* this is either first call to readdir, or rewind. Anyway, create new
	 * cursor. */
	fsdata = create_fsdata(NULL, GFP_KERNEL);
	if (fsdata != NULL) {
		result = radix_tree_preload(GFP_KERNEL);
		if (result == 0) {
			d_cursor_info *info;
			oid_t oid;

			info = d_info(inode);
			oid = get_inode_oid(inode);
			/* cid occupies higher 12 bits of f->f_pos. Don't
			 * allow it to become negative: this confuses
			 * nfsd_readdir() */
			cursor->key.cid = (++cid_counter) & 0x7ff;
			cursor->key.oid = oid;
			cursor->fsdata = fsdata;
			cursor->info = info;
			cursor->ref = 1;
			spin_lock_inode(inode);
			/* install cursor as @f's private_data, discarding old
			 * one if necessary */
			clean_fsdata(f);
			reiser4_free_file_fsdata(f);
			f->private_data = fsdata;
			fsdata->cursor = cursor;
			spin_unlock_inode(inode);
			spin_lock(&d_lock);
			/* insert cursor into hash table */
			d_cursor_hash_insert(&info->table, cursor);
			/* and chain it into radix-tree */
			bind_cursor(cursor, (unsigned long)oid);
			spin_unlock(&d_lock);
			radix_tree_preload_end();
			f->f_pos = ((__u64) cursor->key.cid) << CID_SHIFT;
		}
	} else
		result = RETERR(-ENOMEM);
	return result;
}

/* find or create cursor for readdir-over-nfs */
static int try_to_attach_fsdata(struct file *f, struct inode *inode)
{
	loff_t pos;
	int result;
	dir_cursor *cursor;

	/*
	 * we are serialized by inode->i_sem
	 */

	if (!file_is_stateless(f))
		return 0;

	pos = f->f_pos;
	result = 0;
	if (pos == 0) {
		/*
		 * first call to readdir (or rewind to the beginning of
		 * directory)
		 */
		cursor = kmem_cache_alloc(d_cursor_slab, GFP_KERNEL);
		if (cursor != NULL)
			result = insert_cursor(cursor, f, inode);
		else
			result = RETERR(-ENOMEM);
	} else {
		/* try to find existing cursor */
		d_cursor_key key;

		key.cid = pos >> CID_SHIFT;
		key.oid = get_inode_oid(inode);
		spin_lock(&d_lock);
		cursor = d_cursor_hash_find(&d_info(inode)->table, &key);
		if (cursor != NULL) {
			/* cursor was found */
			if (cursor->ref == 0) {
				/* move it from unused list */
				a_cursor_list_remove_clean(cursor);
				--d_cursor_unused;
			}
			++cursor->ref;
		}
		spin_unlock(&d_lock);
		if (cursor != NULL) {
			spin_lock_inode(inode);
			assert("nikita-3556", cursor->fsdata->back == NULL);
			clean_fsdata(f);
			reiser4_free_file_fsdata(f);
			f->private_data = cursor->fsdata;
			spin_unlock_inode(inode);
		}
	}
	return result;
}

/* detach fsdata, if necessary */
static void detach_fsdata(struct file *f)
{
	struct inode *inode;

	if (!file_is_stateless(f))
		return;

	inode = f->f_dentry->d_inode;
	spin_lock_inode(inode);
	clean_fsdata(f);
	spin_unlock_inode(inode);
}

/*
 * prepare for readdir.
 */
static int dir_readdir_init(struct file *f, tap_t * tap, readdir_pos ** pos)
{
	struct inode *inode;
	reiser4_file_fsdata *fsdata;
	int result;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	/* try to find detached readdir state */
	result = try_to_attach_fsdata(f, inode);
	if (result != 0)
		return result;

	fsdata = reiser4_get_file_fsdata(f);
	assert("nikita-2571", fsdata != NULL);
	if (IS_ERR(fsdata))
		return PTR_ERR(fsdata);

	/* add file descriptor to the readdir list hanging of directory
	 * inode. This list is used to scan "readdirs-in-progress" while
	 * inserting or removing names in the directory. */
	spin_lock_inode(inode);
	if (readdir_list_is_clean(fsdata))
		readdir_list_push_front(get_readdir_list(inode), fsdata);
	*pos = &fsdata->dir.readdir;
	spin_unlock_inode(inode);

	/* move @tap to the current position */
	return dir_rewind(f, *pos, tap);
}

/* this is implementation of vfs's llseek method of struct file_operations for
   typical directory
   See comment before readdir_common() for explanation.
*/
loff_t llseek_common_dir(struct file * file, loff_t off, int origin)
{
	reiser4_context *ctx;
	loff_t result;
	struct inode *inode;

	inode = file->f_dentry->d_inode;

	ctx = init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	down(&inode->i_sem);

	/* update ->f_pos */
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
		detach_fsdata(file);
		if (ff != 0)
			result = (loff_t) ff;
		tap_done(&tap);
	}
	detach_fsdata(file);
	up(&inode->i_sem);

	reiser4_exit_context(ctx);
	return result;
}

/* this is common implementation of vfs's readdir method of struct
   file_operations
 
   readdir problems:
 
   readdir(2)/getdents(2) interface is based on implicit assumption that
   readdir can be restarted from any particular point by supplying file system
   with off_t-full of data. That is, file system fill ->d_off field in struct
   dirent and later user passes ->d_off to the seekdir(3), which is, actually,
   implemented by glibc as lseek(2) on directory.

   Reiser4 cannot restart readdir from 64 bits of data, because two last
   components of the key of directory entry are unknown, which given 128 bits:
   locality and type fields in the key of directory entry are always known, to
   start readdir() from given point objectid and offset fields have to be
   filled.

   Traditional UNIX API for scanning through directory
   (readdir/seekdir/telldir/opendir/closedir/rewindir/getdents) is based on the
   assumption that directory is structured very much like regular file, in
   particular, it is implied that each name within given directory (directory
   entry) can be uniquely identified by scalar offset and that such offset is
   stable across the life-time of the name is identifies.
 
   This is manifestly not so for reiser4. In reiser4 the only stable unique
   identifies for the directory entry is its key that doesn't fit into
   seekdir/telldir API.
 
   solution:
 
   Within each file descriptor participating in readdir-ing of directory
   plugin/dir/dir.h:readdir_pos is maintained. This structure keeps track of
   the "current" directory entry that file descriptor looks at. It contains a
   key of directory entry (plus some additional info to deal with non-unique
   keys that we wouldn't dwell onto here) and a logical position of this
   directory entry starting from the beginning of the directory, that is
   ordinal number of this entry in the readdir order.
 
   Obviously this logical position is not stable in the face of directory
   modifications. To work around this, on each addition or removal of directory
   entry all file descriptors for directory inode are scanned and their
   readdir_pos are updated accordingly (adjust_dir_pos()). 
*/
int readdir_common(struct file *f /* directory file being read */ ,
		   void *dirent /* opaque data passed to us by VFS */ ,
		   filldir_t filld /* filler function passed to us by VFS */ )
{
	reiser4_context *ctx;
	int result;
	struct inode *inode;
	coord_t coord;
	lock_handle lh;
	tap_t tap;
	readdir_pos *pos;

	assert("nikita-1359", f != NULL);
	inode = f->f_dentry->d_inode;
	assert("nikita-1360", inode != NULL);

	if (!S_ISDIR(inode->i_mode))
		return RETERR(-ENOTDIR);

	ctx = init_context(inode->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	coord_init_zero(&coord);
	init_lh(&lh);
	tap_init(&tap, &coord, &lh, ZNODE_READ_LOCK);

	reiser4_readdir_readahead_init(inode, &tap);

      repeat:
	result = dir_readdir_init(f, &tap, &pos);
	if (result == 0) {
		result = tap_load(&tap);
		/* scan entries one by one feeding them to @filld */
		while (result == 0) {
			coord_t *coord;

			coord = tap.coord;
			assert("nikita-2572", coord_is_existing_unit(coord));
			assert("nikita-3227", is_valid_dir_coord(inode, coord));

			result = feed_entry(f, pos, &tap, filld, dirent);
			if (result > 0) {
				break;
			} else if (result == 0) {
				++f->f_pos;
				result = go_next_unit(&tap);
				if (result == -E_NO_NEIGHBOR ||
				    result == -ENOENT) {
					result = 0;
					break;
				} else if (result == 0) {
					if (is_valid_dir_coord(inode, coord))
						move_entry(pos, coord);
					else
						break;
				}
			} else if (result == -E_REPEAT) {
				/* feed_entry() had to restart. */
				++f->f_pos;
				tap_relse(&tap);
				goto repeat;
			} else
				warning("vs-1617",
					"readdir_common: unexpected error %d",
					result);
		}
		tap_relse(&tap);

		if (result >= 0)
			f->f_version = inode->i_version;
	} else if (result == -E_NO_NEIGHBOR || result == -ENOENT)
		result = 0;
	tap_done(&tap);
	detach_fsdata(f);

	context_set_commit_async(ctx);
	reiser4_exit_context(ctx);

	return (result <= 0) ? result : 0;
}