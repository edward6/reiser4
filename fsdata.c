/* Copyright 2001, 2002, 2003, 2004, 2005 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "fsdata.h"
#include "inode.h"


/* cache or dir_cursors */
static kmem_cache_t *d_cursor_cache;
static struct shrinker *d_cursor_shrinker;

/* list of unused cursors */
static a_cursor_list_head cursor_cache = TYPE_SAFE_LIST_HEAD_INIT(cursor_cache);

/* number of cursors in list of ununsed cursors */
static unsigned long d_cursor_unused = 0;

/* spinlock protecting manipulations with dir_cursor's hash table and lists */
static spinlock_t d_lock = SPIN_LOCK_UNLOCKED;

static void kill_cursor(dir_cursor *);

/**
 * d_cursor_shrink - shrink callback for cache of dir_cursor-s
 * @nr: number of objects to free
 * @mask: GFP mask
 *
 * Shrinks d_cursor_cache. Scan LRU list of unused cursors, freeing requested
 * number. Return number of still freeable cursors.
 */
static int d_cursor_shrink(int nr, unsigned int mask)
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

/**
 * init_d_cursor - create d_cursor cache
 *
 * Initializes slab cache of d_cursors. It is part of reiser4 module
 * initialization.
 */
int init_d_cursor(void)
{
	d_cursor_cache = kmem_cache_create("d_cursor", sizeof(dir_cursor), 0,
					   SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (d_cursor_cache == NULL)
		return RETERR(-ENOMEM);

	/*
	 * actually, d_cursors are "priceless", because there is no way to
	 * recover information stored in them. On the other hand, we don't
	 * want to consume all kernel memory by them. As a compromise, just
	 * assign higher "seeks" value to d_cursor cache, so that it will be
	 * shrunk only if system is really tight on memory.
	 */
	d_cursor_shrinker = set_shrinker(DEFAULT_SEEKS << 3,
					 d_cursor_shrink);
	if (d_cursor_shrinker == NULL) {
		kmem_cache_destroy(d_cursor_cache);
		d_cursor_cache = NULL;
		return RETERR(-ENOMEM);
	}
	return 0;
}

/**
 * done_d_cursor - delete d_cursor cache
 *
 * This is called on reiser4 module unloading or system shutdown.
 */
void done_d_cursor(void)
{
	BUG_ON(d_cursor_shrinker != NULL);
	remove_shrinker(d_cursor_shrinker);
	d_cursor_shrinker = NULL;

	destroy_reiser4_cache(&d_cursor_cache);
}

#define D_CURSOR_TABLE_SIZE (256)

static inline unsigned long
d_cursor_hash(d_cursor_hash_table *table, const d_cursor_key *key)
{
	assert("nikita-3555", IS_POW(D_CURSOR_TABLE_SIZE));
	return (key->oid + key->cid) & (D_CURSOR_TABLE_SIZE - 1);
}

static inline int d_cursor_eq(const d_cursor_key *k1, const d_cursor_key *k2)
{
	return k1->cid == k2->cid && k1->oid == k2->oid;
}

/*
 * define functions to manipulate reiser4 super block's hash table of
 * dir_cursors
 */
#define KMALLOC(size) kmalloc((size), GFP_KERNEL)
#define KFREE(ptr, size) kfree(ptr)
TYPE_SAFE_HASH_DEFINE(d_cursor,
		      dir_cursor,
		      d_cursor_key, key, hash, d_cursor_hash, d_cursor_eq);
#undef KFREE
#undef KMALLOC

/**
 * init_super_d_info - initialize per-super-block d_cursor resources
 * @super: super block to initialize
 *
 * Initializes per-super-block d_cursor's hash table and radix tree. It is part
 * of mount.
 */
int init_super_d_info(struct super_block *super)
{
	d_cursor_info *p;

	p = &get_super_private(super)->d_info;

	INIT_RADIX_TREE(&p->tree, GFP_KERNEL);
	return d_cursor_hash_init(&p->table, D_CURSOR_TABLE_SIZE);
}

/**
 * done_super_d_info - release per-super-block d_cursor resources
 * @super: super block being umounted
 *
 * Frees hash table. Radix tree of d_cursors has nothing to free. It is called
 * on umount.
 */
void d_cursor_done_at(struct super_block *super)
{
	BUG_ON(get_super_private(super)->d_info.tree.rnode == NULL);
	d_cursor_hash_done(&get_super_private(super)->d_info.table);
}

/**
 * kill_cursor - free dir_cursor and reiser4_file_fsdata attached to it
 * @cursor: cursor to free
 *
 * Removes reiser4_file_fsdata attached to @cursor from readdir list of
 * reiser4_inode, frees that reiser4_file_fsdata. Removes @cursor from from
 * indices, hash table, list of unused cursors and frees it.
 */
static void kill_cursor(dir_cursor *cursor)
{
	unsigned long index;

	assert("nikita-3566", cursor->ref == 0);
	assert("nikita-3572", cursor->fsdata != NULL);

	index = (unsigned long)cursor->key.oid;
	readdir_list_remove_clean(cursor->fsdata);
	free_fsdata(cursor->fsdata);
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
	kmem_cache_free(d_cursor_cache, cursor);
	--d_cursor_unused;
}



/* slab for reiser4_dentry_fsdata */
static kmem_cache_t *dentry_fsdata_cache;

/**
 * init_dentry_fsdata - create cache of dentry_fsdata
 *
 * Initializes slab cache of structures attached to denty->d_fsdata. It is
 * part of reiser4 module initialization.
 */
int init_dentry_fsdata(void)
{
	dentry_fsdata_cache = kmem_cache_create("dentry_fsdata",
						sizeof(reiser4_dentry_fsdata),
						0,
						SLAB_HWCACHE_ALIGN |
						SLAB_RECLAIM_ACCOUNT, NULL,
						NULL);
	if (dentry_fsdata_cache == NULL)
		return RETERR(-ENOMEM);
	return 0;
}

/**
 * done_dentry_fsdata - delete cache of dentry_fsdata
 *
 * This is called on reiser4 module unloading or system shutdown.
 */
void done_dentry_fsdata(void)
{
	destroy_reiser4_cache(&dentry_fsdata_cache);
}

/**
 * reiser4_get_dentry_fsdata - get fs-specific dentry data 
 * @dentry: queried dentry
 *
 * Allocates if necessary and returns per-dentry data that we attach to each
 * dentry.
 */
reiser4_dentry_fsdata *reiser4_get_dentry_fsdata(struct dentry *dentry)
{
	assert("nikita-1365", dentry != NULL);

	if (dentry->d_fsdata == NULL) {
		dentry->d_fsdata = kmem_cache_alloc(dentry_fsdata_cache,
						    GFP_KERNEL);
		if (dentry->d_fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));
		memset(dentry->d_fsdata, 0, sizeof(reiser4_dentry_fsdata));
	}
	return dentry->d_fsdata;
}

/**
 * reiser4_free_dentry_fsdata - detach and free dentry_fsdata
 * @dentry: dentry to free fsdata of
 *
 * Detaches and frees fs-specific dentry data
 */
void reiser4_free_dentry_fsdata(struct dentry *dentry)
{
	if (dentry->d_fsdata != NULL) {
		kmem_cache_free(dentry_fsdata_cache, dentry->d_fsdata);
		dentry->d_fsdata = NULL;
	}
}


/* slab for reiser4_file_fsdata */
static kmem_cache_t *file_fsdata_cache;

/**
 * init_file_fsdata - create cache of reiser4_file_fsdata
 *
 * Initializes slab cache of structures attached to file->private_data. It is
 * part of reiser4 module initialization.
 */
int init_file_fsdata(void)
{
	file_fsdata_cache = kmem_cache_create("file_fsdata",
					      sizeof(reiser4_file_fsdata),
					      0,
					      SLAB_HWCACHE_ALIGN |
					      SLAB_RECLAIM_ACCOUNT, NULL, NULL);
	if (file_fsdata_cache == NULL)
		return RETERR(-ENOMEM);
	return 0;
}

/**
 * done_file_fsdata - delete cache of reiser4_file_fsdata
 *
 * This is called on reiser4 module unloading or system shutdown.
 */
void done_file_fsdata(void)
{
	destroy_reiser4_cache(&file_fsdata_cache);
}

/**
 * create_fsdata - allocate and initialize reiser4_file_fsdata
 * @file: what to create file_fsdata for, may be NULL
 *
 * Allocates and initializes reiser4_file_fsdata structure.
 */
reiser4_file_fsdata *create_fsdata(struct file *file)
{
	reiser4_file_fsdata *fsdata;

	fsdata = kmem_cache_alloc(file_fsdata_cache, GFP_KERNEL);
	if (fsdata != NULL) {
		memset(fsdata, 0, sizeof *fsdata);
		fsdata->ra1.max_window_size = VM_MAX_READAHEAD * 1024;
		fsdata->back = file;
		readdir_list_clean(fsdata);
	}
	return fsdata;
}

/**
 * free_fsdata - free reiser4_file_fsdata
 * @fsdata: object to free
 *
 * Dual to create_fsdata(). Free reiser4_file_fsdata.
 */
void free_fsdata(reiser4_file_fsdata *fsdata)
{
	BUG_ON(fsdata == NULL);
	kmem_cache_free(file_fsdata_cache, fsdata);
}

/**
 * reiser4_get_file_fsdata - get fs-specific file data 
 * @file: queried file
 *
 * Returns fs-specific data of @file. If it is NULL, allocates it and attaches
 * to @file.
 */
reiser4_file_fsdata *reiser4_get_file_fsdata(struct file *file)
{
	assert("nikita-1603", file != NULL);

	if (file->private_data == NULL) {
		reiser4_file_fsdata *fsdata;
		struct inode *inode;

		fsdata = create_fsdata(file);
		if (fsdata == NULL)
			return ERR_PTR(RETERR(-ENOMEM));

		inode = file->f_dentry->d_inode;
		spin_lock_inode(inode);
		if (file->private_data == NULL) {
			file->private_data = fsdata;
			fsdata = NULL;
		}
		spin_unlock_inode(inode);
		if (fsdata != NULL)
			/* other thread initialized ->fsdata */
			kmem_cache_free(file_fsdata_cache, fsdata);
	}
	assert("nikita-2665", file->private_data != NULL);
	return file->private_data;
}

/**
 * reiser4_free_file_fsdata - detach from struct file and free reiser4_file_fsdata
 * @file:
 *
 * Detaches reiser4_file_fsdata from @file, removes reiser4_file_fsdata from
 * readdir list, frees if it is not linked to d_cursor object.
 */
void reiser4_free_file_fsdata(struct file *file)
{
	reiser4_file_fsdata *fsdata;

	spin_lock_inode(file->f_dentry->d_inode);
	fsdata = file->private_data;
	if (fsdata != NULL) {
		readdir_list_remove_clean(fsdata);
		if (fsdata->cursor == NULL)
			free_fsdata(fsdata);
	}
	file->private_data = NULL;

	spin_unlock_inode(file->f_dentry->d_inode);
}



/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 120
 * End:
 */
