/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation.
 */
#define _GNU_SOURCE

#include "../reiser4.h"


int fs_is_here;

void panic( const char *format, ... )
{
	va_list args;

	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	if( getenv( "REISER4_CRASH_MODE" ) &&
	    !strcmp( getenv( "REISER4_CRASH_MODE" ), "debugger" ) ) {
		abend();
	} else {
		abort();
	}
}

#if defined( REISER4_SILENT ) && REISER4_SILENT
int printf( const char *format UNUSED_ARG, ... )
{
	return 0;
}
#endif

int sema_init( semaphore *sem, int value )
{
	pthread_mutex_init( &sem -> mutex, NULL );
	if( value == 0 )
		pthread_mutex_lock( &sem -> mutex );
	return 0;
}

int init_MUTEX_LOCKED( semaphore *sem )
{
	return sema_init( sem, 0 );
}

int down_interruptible( semaphore *sem )
{
	return pthread_mutex_lock( &sem -> mutex );
}

void down( semaphore *sem )
{
#if 1
	pthread_mutex_lock( &sem -> mutex );
#else
	unsigned long start, now, diff;
	start = jiffies;
	for (;;) {
		int ret = pthread_mutex_trylock (&sem->mutex);

		if (ret == 0) {
			return;
		}

		now  = jiffies;
		diff = now - start;

		/* check if we've been sleeping 10s */
		if (diff > 10 * 1000 * 1000) {
			rpanic ("jmacd-1000", "down() too long!");
		}

		/* sleep 100ms */
		usleep (100 * 1000);
	} 
#endif
}

void up( semaphore *sem )
{
	pthread_mutex_unlock( &sem -> mutex );
}

void show_stack( unsigned long * esp UNUSE )
{
}

void lock_kernel()
{
}

void unlock_kernel()
{
}

void cond_resched()
{
	sched_yield();
}

void schedule()
{
	sched_yield();
}

void spinlock_bug (const char *msg)
{
	rpanic ("jmacd-1010", "spinlock: %s", msg); 
}

#define KMEM_CHECK 0
#define KMEM_MAGIC 0x74932123U

__u64 total_allocations = 0ull;

void *xxmalloc( size_t size )
{
	++ total_allocations;

	if( total_allocations > MEMORY_PRESSURE_THRESHOLD )
		declare_memory_pressure();
	return malloc( size );
}

void xfree( void *addr )
{
	free( addr );
}

void *kmalloc( size_t size, int flag UNUSE )
{
	__u32 *addr;

#if KMEM_CHECK	
	size += sizeof (__u32);
#endif

	addr = xxmalloc( size );

#if KMEM_CHECK
	if (addr != NULL) {
		*addr = KMEM_MAGIC;
		addr += 1;
	}
#endif

	return addr;
}

void kfree( void *addr )
{
	__u32 *check = addr;
	
#if KMEM_CHECK	
	check -= 1;

	assert( "jmacd-1065", *check == KMEM_MAGIC);

	*check = 0;
#endif
	
	free( check );
}

/* 
    Audited by umka (2002.06.13)
    Here in kmalloc request should be exactly specified the kind of request
    GFP_KERNEL, etc.
*/
kmem_cache_t *kmem_cache_create( const char *name, 
				 size_t size UNUSED_ARG, 
				 size_t offset UNUSED_ARG,
				 unsigned long flags UNUSED_ARG, 
				 void (*ctor)(void*, kmem_cache_t *, unsigned long) UNUSED_ARG,
				 void (*dtor)(void*, kmem_cache_t *, unsigned long) UNUSED_ARG )
{
	kmem_cache_t *result;

	result = kmalloc( sizeof *result, 0 );
	result -> size  = size;
	result -> count = 0;
	result -> name  = name;
	spin_lock_init (& result -> lock);
	return result;
}

int kmem_cache_destroy( kmem_cache_t *slab )
{
	if (slab -> count != 0) {
		warning ("jmacd-1065", "%s slab allocator still has %u objects allocated", slab -> name, slab -> count);
	}
	
	kfree( slab );
	return 0;
}

void kmem_cache_free( kmem_cache_t *slab, void *addr )
{
	assert( "jmacd-1064", addr != NULL);

	kfree( addr );

	spin_lock (& slab -> lock);
	if (slab -> count == 0) {
		rpanic ("jmacd-1066", "%s slab allocator: too many frees", slab -> name);
	}
	slab -> count -= 1;
	spin_unlock (& slab -> lock);
}

/* 
    Audited by umka (2002.06.13)
    xmemset may try access NULL addr in the case kmalloc 
    will unable to allocate specified size.
*/
void *kmem_cache_alloc( kmem_cache_t *slab, int gfp_flag UNUSE )
{
	void *addr;

	addr = kmalloc( slab -> size, 0 );

	if (addr) {
		xmemset( addr, 0, slab -> size );
		spin_lock (& slab -> lock);
		slab -> count += 1;
		spin_unlock (& slab -> lock);
	}

	return addr;
}

unsigned long event = 0;


/****************************************************************************/

struct file_system_type *file_systems [1];

int register_filesystem (struct file_system_type * fs)
{
	if (file_systems [0])
		BUG ();
	file_systems [0] = fs;
	return 0;
}

struct super_block super_blocks[1];
struct block_device block_devices[1];

struct super_block * get_sb_bdev (struct file_system_type *fs_type UNUSED_ARG,
				  int flags, char *dev_name, 
				  void * data,
				  int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block * s;
	int result;

	s = &super_blocks[0];
	s->s_flags = flags;
	s->s_blocksize = PAGE_CACHE_SIZE;
	s->s_blocksize_bits = PAGE_CACHE_SHIFT;
	s->s_bdev = &block_devices[0];
	s->s_bdev->bd_dev = open (dev_name, O_RDWR);
	if (s->s_bdev->bd_dev == -1)
		return ERR_PTR (errno);

	result = fill_super (s, data, 0/*silent*/);

	if (result)
		return ERR_PTR (result);
	return s;
}


static struct file_system_type * find_filesystem (const char * name)
{
	if (file_systems[0] == 0)
		BUG ();
	if (strcmp (name, file_systems[0]->name))
		BUG ();
	return file_systems [0];
}



static struct super_block * call_mount (const char * dev_name, char *opts)
{
	struct file_system_type * fs;

	fs = find_filesystem ("reiser4");
	if (!fs)
		return 0;

	return fs->get_sb (fs, 0/*flags*/, (char *)dev_name, opts);
}



/****************************************************************************/


static spinlock_t inode_hash_guard;
struct list_head inode_hash_list;

#if 0
static node_operations ul_tops = {
	.read_node     = ulevel_read_node,
	.allocate_node = ulevel_read_node,
	.delete_node   = NULL,
	.release_node  = ulevel_release_node,
	.dirty_node    = ulevel_dirty_node,
	.drop_node     = NULL
};
#endif

static struct inode * alloc_inode (struct super_block * sb)
{
	struct inode * inode;

	inode = sb->s_op->alloc_inode(sb);
	assert ("vs-289", inode);
	xmemset (inode, 0, sizeof (struct inode));
	inode->i_sb = sb;
	inode->i_dev = sb->s_dev;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = 0;
	atomic_set(&inode->i_count, 1);
	inode->i_data.host = inode;
	inode->i_mapping = &inode->i_data;
	INIT_LIST_HEAD (&inode->i_mapping->clean_pages);
	INIT_LIST_HEAD (&inode->i_mapping->dirty_pages);
	INIT_LIST_HEAD (&inode->i_mapping->locked_pages);
	return inode;
}

struct inode * new_inode (struct super_block * sb)
{
	struct inode * inode;

	inode = alloc_inode (sb);
	inode->i_nlink = 1;
	spin_lock( &inode_hash_guard );
	list_add (&inode->i_hash, &inode_hash_list);
	spin_unlock( &inode_hash_guard );
	return inode;
}


int init_special_inode( struct inode *inode UNUSED_ARG, __u32 mode UNUSED_ARG,
			int rdev UNUSED_ARG )
{
	return 0;
}


void make_bad_inode( struct inode *inode UNUSED_ARG )
{
}

int is_bad_inode( struct inode *inode UNUSED_ARG )
{
	return 0;
}


struct inode * find_inode (struct super_block *super UNUSED_ARG,
			   unsigned long ino, 
			   int (*test)(struct inode *, void *), 
			   void *data)
{
	struct list_head * cur;
	struct inode * inode;

	list_for_each (cur, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		trace_on( TRACE_VFS_OPS, 
			  "inode: %li, %li\n", inode->i_ino, ino );
		if (inode->i_ino != ino)
			continue;
		if (!test || !test(inode, data))
			continue;
		return inode;
	}
	return 0;
}


/* remove all inodes from their list */
static void invalidate_inodes (void)
{
	struct list_head * cur, * tmp;
	struct inode * inode;

	list_for_each_safe (cur, tmp, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		if (atomic_read (&inode->i_count) || (inode->i_state & I_DIRTY))
			print_inode ("invalidate_inodes", inode);
		spin_lock( &inode_hash_guard );
		list_del_init( &inode -> i_hash );
		spin_unlock( &inode_hash_guard );
	}
}


void iput( struct inode *inode )
{
	if( !inode )
		return;
	if( atomic_dec_and_test( & inode -> i_count) ) {
		/* i_count drops to 0, call release
		 * FIXME-VS: */
		struct file file;
		struct dentry dentry;

		xmemset (&dentry, 0, sizeof dentry);
		xmemset (&file, 0, sizeof file);
		file.f_dentry = &dentry;
		dentry.d_inode = inode;
		if (inode->i_fop && inode->i_fop->release (inode, &file))
			info ("release failed");

		spin_lock( &inode_hash_guard );
		/*list_del_init( &inode -> i_hash );*/
		spin_unlock( &inode_hash_guard );
	}
}


/*
 * FIXME-VS: these are copied from reiser4/inode.c
 */
static int ul_init_locked_inode( struct inode *inode /* new inode */, 
				 void *opaque /* key of stat data passed to the
					    * iget5_locked as cookie */ )
{
	reiser4_key *key;

	assert( "nikita-1947", inode != NULL );
	assert( "nikita-1948", opaque != NULL );
	key = opaque;
	inode -> i_ino = get_key_objectid( key );
	reiser4_inode_data( inode ) -> locality_id = get_key_locality( key );
	return 0;
}

static int ul_find_actor( struct inode *inode /* inode from hash table to
					       * check */,
			  void *opaque /* "cookie" passed to iget5_locked(). This
					* is stat data key */ )
{
    reiser4_key *key;

    key = opaque;
    return 
	    ( inode -> i_ino == get_key_objectid( key ) ) &&
	    ( reiser4_inode_data( inode ) -> locality_id == get_key_locality( key ) );
}


struct inode *
get_new_inode(struct super_block *sb, 
	      unsigned long hashval, 
	      int (*test)(struct inode *, void *), 
	      int (*set)(struct inode *, void *), void *data)
{
	struct inode * inode;

	inode = alloc_inode(sb);
	if (inode) {
		struct inode * old;

		spin_lock(&inode_hash_guard);
		/* We released the lock, so.. */
		old = find_inode(sb, hashval, test, data);
		if (!old) {
			if (set != NULL) {
				if (set(inode, data))
					goto set_failed;
			} else {
				inode->i_ino = hashval;
			}

			inode->i_state = I_LOCK|I_NEW;
			spin_unlock(&inode_hash_guard);

			return inode;
		}

		/*
		 * Uhhuh, somebody else created the same inode under
		 * us. Use the old inode instead of the one we just
		 * allocated.
		 */
		atomic_inc(&old->i_count);
		spin_unlock(&inode_hash_guard);
		inode = old;
	}
	return inode;

set_failed:
	spin_unlock(&inode_hash_guard);
	return NULL;
}

struct inode *
iget5_locked(struct super_block *sb, 
	     unsigned long hashval, 
	     int (*test)(struct inode *, void *), 
	     int (*set)(struct inode *, void *), void *data)
{
	struct inode * inode;

	spin_lock( &inode_hash_guard );
	inode = find_inode (sb, hashval, test, data);
	if (inode) {
		atomic_inc(&inode->i_count);
		spin_unlock( &inode_hash_guard );
		return inode;
	}
	spin_unlock( &inode_hash_guard );

	/*
	 * get_new_inode() will do the right thing, re-trying the search
	 * in case it had to block at any point.
	 */
	return get_new_inode(sb, hashval, test, set, data);
}

struct inode *iget_locked(struct super_block *sb, unsigned long ino)
{
	return iget5_locked (sb, ino, NULL, NULL, NULL);
}

void mark_inode_dirty (struct inode * inode)
{
	inode->i_state |= I_DIRTY;
}


void d_instantiate(struct dentry *entry, struct inode * inode)
{
	entry -> d_inode = inode;
}


void d_add(struct dentry * entry, struct inode * inode )
{
	entry -> d_inode = inode;
}

pthread_key_t __current_key;

__u32 set_current ()
{
	if (current == NULL) {
		int ret;
		struct task_struct *self;

		self = malloc (sizeof (struct task_struct));
		self->journal_info = 0;
		if ((ret = pthread_setspecific (__current_key, self)) != 0) {
			rpanic ("jmacd-900", "pthread_setspecific failed");
		}
	}

	return (__u32) pthread_self ();
}

void free_current (void* arg)
{
	if (arg != NULL) {
		free (arg);
	}
}

ssize_t generic_file_read(struct file *f UNUSED_ARG, char *b UNUSED_ARG,
			  size_t l UNUSED_ARG, loff_t *o UNUSED_ARG)
{
	return 0;
}

ssize_t generic_file_write(struct file *f UNUSED_ARG, const char *b UNUSED_ARG,
			   size_t l UNUSED_ARG, loff_t *o UNUSED_ARG)
{
	return 0;
}

void insert_inode_hash(struct inode *inode UNUSED_ARG)
{
}


int __copy_from_user (char * to, char * from, unsigned n)
{
	xmemcpy (to, from, n);
	return 0;
}


int __copy_to_user (char * to, char * from, unsigned n)
{
	xmemcpy (to, from, n);
	return 0;
}


/* mm/swap.c */

void lru_cache_del (struct page * page UNUSED_ARG)
{
	return;
}


/* mm/filemap.c */

struct list_head page_list;
static spinlock_t page_list_guard;

static void init_page (struct page * page, struct address_space * mapping,
		       unsigned long ind)
{
	page->index = ind;
	page->mapping = mapping;
	page->private = 0;
	atomic_set (&page->count, 1);
	/* use kmap to set this */
	page->virtual = 0;
	spin_lock_init (&page->lock);
	spin_lock_init (&page->lock2);

	INIT_LIST_HEAD (&page -> mapping_list);
	spin_lock( &page_list_guard );
	list_add (&page->list, &page_list);
	spin_unlock( &page_list_guard );
}

static struct page * new_page (struct address_space * mapping,
			       unsigned long ind)
{
	struct page * page;

	page = kmalloc (sizeof (struct page) + PAGE_CACHE_SIZE, 0);
	assert ("vs-288", page);
	xmemset (page, 0, sizeof (struct page) + PAGE_CACHE_SIZE);

	init_page (page, mapping, ind);
	return page;
}


void lock_page (struct page * p)
{
	spin_lock (&p->lock);
	SetPageLocked (p);
}


void unlock_page (struct page * p)
{
	assert ("vs-286", PageLocked (p));
	ClearPageLocked (p);
	spin_unlock (&p->lock);
}


void remove_inode_page (struct page * page)
{
	assert ("vs-618", atomic_read (&page->count) == 2);
	page->mapping = 0;

	txn_delete_page (page);
}


struct page * find_get_page (struct address_space * mapping,
			     unsigned long ind)
{
	struct list_head * cur;
	struct page * page;


	list_for_each (cur, &page_list) {
		page = list_entry (cur, struct page, list);
		spin_lock (&page->lock2);
		if (page->index == ind && page->mapping == mapping) {
			atomic_inc (&page->count);
			spin_unlock (&page->lock2);
			return page;
		}
		spin_unlock (&page->lock2);
	}
	return 0;
}


static void truncate_inode_pages (struct address_space * mapping,
				  loff_t from)
{
	struct list_head * tmp;
	struct list_head * cur;
	struct page * page;
	unsigned ind;


	ind = (from + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	list_for_each_safe (cur, tmp, &page_list) {
		page = list_entry (cur, struct page, list);
		if (page->mapping == mapping) {
			if (page->index >= ind) {
				spin_lock (&page->lock2);
				atomic_inc (&page->count);
				remove_inode_page (page);
				atomic_dec (&page->count);
				if (!atomic_read (&page->count)) {
					spin_lock( &page_list_guard );
					list_del_init (&page->list);
					spin_unlock( &page_list_guard );
					free (page);
				}
				spin_unlock (&page->lock2);
			}
		}
	}
}


struct page * find_lock_page (struct address_space * mapping,
			      unsigned long ind)
{
	struct page * page;

	page = find_get_page (mapping, ind);
	if (page)
		lock_page (page);
	return page;
}


/* this increases page->count and locks the page */
struct page * grab_cache_page (struct address_space * mapping,
			       unsigned long ind)
{
	struct page * page;


	page = find_lock_page (mapping, ind);
	if (page)
		return page;

	page = new_page (mapping, ind);
	lock_page (page);
	return page;
}


/* look for page in cache, if it does not exist - create a new one and
   call @filler */
struct page *read_cache_page (struct address_space * mapping,
			      unsigned long idx,
			      int (* filler)(void *, struct page *),
			      void *data)
{
	struct page * page;

	page = find_get_page (mapping, idx);
	if (!page)
		page = new_page (mapping, idx);
	
	if (!PageUptodate (page)) {
		lock_page (page);
		filler (data, page);
	}
	return page;
}

void page_cache_print()
{
	struct list_head * cur;

	list_for_each (cur, &page_list)
		print_page( list_entry ( cur, struct page, list ) );
}

int generic_file_mmap(struct file * file UNUSED_ARG,
		      struct vm_area_struct * vma UNUSED_ARG)
{
	return 0;
}




void wait_on_page_locked(struct page * page)
{
	if (PageLocked (page)) {
		reiser4_stat_file_add (wait_on_page);
		unlock_page (page);
	}
}

void wait_on_page_writeback(struct page * page)
{
	return;
}


/* mm/readahead.c */
void page_cache_readahead (struct file * file UNUSED_ARG, unsigned long offset UNUSED_ARG)
{
	return;
}


static void invalidate_pages (void)
{
	struct list_head * cur, * tmp;
	struct page * page;

	spin_lock( &page_list_guard );
	list_for_each_safe (cur, tmp, &page_list) {
		page = list_entry (cur, struct page, list);
		spin_lock (&page->lock2);
		lock_page (page);
		assert ("vs-666", !PageDirty (page));
		assert ("vs-667", atomic_read (&page->count) == 0);
		assert ("vs-668", !page->kmap_count);
		list_del_init( &page -> list );
		free (page);
		spin_unlock (&page->lock2);
	}
	spin_unlock( &page_list_guard );
}

void print_pages (void)
{
	struct list_head * cur;
	struct page * page;

	list_for_each (cur, &page_list) {
		page = list_entry (cur, struct page, list);
		print_page (page);
	}
}


void print_inodes (void)
{
	struct list_head * cur;
	struct inode * inode;

	list_for_each (cur, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		print_inode ("", inode);
	}
}


char * kmap (struct page * page)
{
	spin_lock (&page->lock2);
	if (!page->kmap_count) {
		page->virtual = (char *)page + sizeof (struct page);
	}
	page->kmap_count ++;
	spin_unlock (&page->lock2);
	return page->virtual;
}


void kunmap (struct page * page)
{
	assert ("vs-724", page->kmap_count > 0);
	spin_lock (&page->lock2);
	page->kmap_count --;
	if (page->kmap_count == 0) {
		page->virtual = 0;
	}
	spin_unlock (&page->lock2);
}

unsigned long get_jiffies ()
{
	struct timeval tv;
	if (gettimeofday (&tv, NULL) < 0) {
		rpanic ("jmacd-1001", "gettimeofday failed");
	}
	/* Assume a HZ of 1e6 */
	return (tv.tv_sec * 1e6 + tv.tv_usec);
}

void page_cache_get(struct page * page)
{
	spin_lock( &page_list_guard );
	atomic_inc (&page->count);
	spin_unlock( &page_list_guard );
}

/* mm/page_alloc.c */
void page_cache_release (struct page * page)
{
	spin_lock( &page_list_guard );
	assert ("vs-352", atomic_read (&page->count) > 0);
	atomic_dec (&page->count);
	spin_unlock( &page_list_guard );
}


/* fs/buffer.c */
int fsync_bdev(struct block_device * bdev)
{
	struct list_head * cur;
	struct page * page;
	jnode * j;

	list_for_each (cur, &page_list) {
		struct buffer_head bh, *pbh;

		page = list_entry (cur, struct page, list);
		if (!PageDirty (page))
			continue;
		if (PagePrivate (page))
			j = (jnode *)page->private;
		else if(page->mapping->host == get_current_super_private() -> fake)
			j = (jnode *)page->index;
		else {
			info ("dirty page does not have jnode\n");
			ClearPageDirty (page);
			continue;
		}
		bh.b_size = reiser4_get_current_sb ()->s_blocksize;
		bh.b_blocknr = j->blocknr;
		bh.b_bdev = bdev;
		bh.b_data = kmap (page);
		pbh = &bh;
		ll_rw_block (WRITE, 1, &pbh);
		kunmap (page);
		ClearPageDirty (page);
		// jnode_detach_page (j);
	}
	return 0;
}


sector_t generic_block_bmap(struct address_space *mapping,
			    sector_t block, get_block_t *get_block)
{
	struct buffer_head tmp;
        struct inode *inode = mapping->host;
        tmp.b_state = 0;
        tmp.b_blocknr = 0;
        get_block(inode, block, &tmp, 0);
        return tmp.b_blocknr;	
}


/* drivers/block/ll_rw_block.c */
void ll_rw_block (int rw, int nr, struct buffer_head ** pbh)
{
	int i;

	for (i = 0; i < nr; i ++) {
		if (lseek64 (pbh[i]->b_bdev->bd_dev,
			     pbh[i]->b_size * (off64_t)pbh[i]->b_blocknr,
			     SEEK_SET) != 
		    pbh[i]->b_size * (off64_t)pbh[i]->b_blocknr, SEEK_SET) {
			info ("ll_rw_block: lsek64 failed\n");
			return;
		}
		if (rw == READ) {
			if (read (pbh[i]->b_bdev->bd_dev, pbh[i]->b_data, pbh[i]->b_size) != 
			    (ssize_t)pbh[i]->b_size) {
				info ("ll_rw_block: read failed\n");
				clear_buffer_uptodate (pbh[i]);
			} else {
				set_buffer_uptodate (pbh[i]);
			}
		} else {
			if (write (pbh[i]->b_bdev->bd_dev, pbh[i]->b_data, pbh[i]->b_size) !=
			    (ssize_t)pbh[i]->b_size) {
				info ("ll_rw_block: read failed\n");
			}
		}
	}
}


void mark_buffer_async_read (struct buffer_head * bh UNUSED_ARG)
{
	
}

int submit_bh (int rw UNUSED_ARG, struct buffer_head * bh UNUSED_ARG)
{
	return 0;
}

void wait_on_buffer (struct buffer_head * bh)
{
	if (buffer_locked (bh)) {
		unlock_buffer (bh);
		set_buffer_uptodate (bh);
	}
}



#define STYPE( type ) info( #type "\t%i\n", sizeof( type ) )

char *__prog_name;

static int mmap_back_end_fd = -1;
static char *mmap_back_end_start = NULL;
static size_t mmap_back_end_size = 0;

int submit_bio( int rw, struct bio *bio )
{
	int i;
	int success;
	int fd;

	assert ("jmacd-997", (rw == WRITE) || (rw == READ));

	fd = bio -> bi_bdev -> bd_dev;
	success = lseek64( fd, ( off64_t )( bio -> bi_sector * 512 ), SEEK_SET );
	if( success == ( off64_t )-1 ) {
		perror( "lseek64" );
		return success;
	}
	success = 1;
	for( i = 0; ( i < bio -> bi_vcnt ) && success ; ++ i ) {
		struct bio_vec *bvec;
		char  *addr;
		size_t count;
		struct page *pg;

		bvec = & bio -> bi_io_vec[ i ];
		pg = bvec -> bv_page;
		kmap( pg );
		addr = ( char * ) page_address( pg ) + bvec -> bv_offset;
		count = bvec -> bv_len;
		if( rw == READ ) {
			if( read( fd, addr, count) != (ssize_t)count ) {
				info( "submit_bio: read failed\n" );
				success = 0;
			}
		} else if( rw == WRITE ) {
			if( write( fd, addr, count ) != (ssize_t)count ) {
				perror( "submit_bio: write failed\n" );
				success = 0;
			}
		}
		kunmap( pg );
	}

	if( success )
		set_bit( BIO_UPTODATE, &bio -> bi_flags );
	else
		clear_bit( BIO_UPTODATE, &bio -> bi_flags );
	bio -> bi_end_io( bio );
	return success ? 0 : -1;
}

#if 0
int ulevel_read_node( reiser4_tree *tree, jnode *node )
{
	const reiser4_block_nr *addr;
	unsigned int blksz;
	struct page *pg;

	addr = jnode_get_block( node );
	blksz = tree -> super -> s_blocksize;
	if( ( mmap_back_end_fd > 0 ) && !blocknr_is_fake( addr ) ) {
		off_t start;

		pg = *addr * ( blksz + sizeof *pg );
		start = pg + 1;
		init_page (pg, get_super_private( tree -> super ) -> fake -> i_mapping,
			   node);
		if( start + blksz > mmap_back_end_size ) {
			warning( "nikita-1372", "Trying to access beyond the device: %li > %u",
				 start, mmap_back_end_size );
			return -EIO;
		} else {
			++ total_allocations;

			if (total_allocations > MEMORY_PRESSURE_THRESHOLD)
				declare_memory_pressure();

			node -> pg = pg;
			return 0;
		}
	} else {
		node -> pg = xxmalloc( blksz + sizeof *pg );
		if( node -> pg != NULL ) {
			init_page (pg, 
				   get_super_private( tree -> super ) -> fake -> i_mapping, node);
			return 0;
		} else
			return -ENOMEM;
	}
}

int ulevel_release_node( reiser4_tree *tree UNUSED_ARG, jnode *node UNUSED_ARG )
{
	return 0;
}

int ulevel_dirty_node( reiser4_tree *tree UNUSED_ARG, jnode *node UNUSED_ARG )
{
	assert ("vs-688", JF_ISSET (node, ZNODE_LOADED));
	set_page_dirty (jnode_page (node));
	return 0;
}
#endif

static struct buffer_head * getblk (struct super_block * sb, int block)
{
	struct buffer_head * bh;

	bh = malloc (sizeof *bh);
	if (!bh)
		return 0;
	bh->b_data = malloc (sb->s_blocksize);
	if (!bh->b_data) {
		free (bh);
		return 0;
	}
	bh->b_count = 1;
	bh->b_blocknr = block;
	bh->b_size = sb->s_blocksize;
	bh->b_bdev = sb->s_bdev;
	return bh;
}

struct buffer_head * sb_bread (struct super_block * sb, int block)
{
	struct buffer_head * bh;


	bh = getblk (sb, block);
	if (!bh)
		return 0;

	if (lseek64 (sb->s_bdev->bd_dev, bh->b_size * (off64_t)block, SEEK_SET) != 
	    bh->b_size * (off64_t)block, SEEK_SET) {
		brelse (bh);
		return 0;
	}
	if (read (sb->s_bdev->bd_dev, bh->b_data, bh->b_size) != (int)bh->b_size) {
		brelse (bh);
		return 0;
	}
	return bh;
}


void brelse (struct buffer_head * bh)
{
	assert ("vs-472", bh->b_count > 0);
	if (bh->b_count -- == 1) {
		if (buffer_dirty (bh)) {
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
		}
		free (bh->b_data);
		free (bh);
	}
}


int block_read_full_page (struct page *page,
			  get_block_t *get_block)
{
        struct inode *inode = page->mapping->host;
        unsigned long iblock, lblock, i;
        struct buffer_head *arr[MAX_BUF_PER_PAGE], bhs[MAX_BUF_PER_PAGE];
        unsigned int blocksize, blocksize_bits, blocks;
        int nr, j;
	char * data;


	/*
	 * FIXME-VS: inode->i_sb is a fake super block. Its blocksize is
	 * wrong. Wouldn't it make troubles to real block_read_full_page
	 */
	blocksize = reiser4_get_current_sb ()->s_blocksize;
	blocksize_bits = reiser4_get_current_sb ()->s_blocksize_bits;


        blocks = PAGE_CACHE_SIZE >> blocksize_bits;
        iblock = page->index << (PAGE_CACHE_SHIFT - blocksize_bits);
	/*
	 * FIXME-VS: assume block device endless
	 */
        lblock = ~0ul;/*(inode->i_size+blocksize-1) >> blocksize_bits;*/

        nr = 0;
        i = 0;
	data = kmap (page);

	for (i = 0; i < blocks; i ++, data += blocksize, iblock ++) {
		bhs[i].b_state = 0;
		bhs[i].b_size = blocksize;
		bhs[i].b_data = data;

		if (iblock < lblock) {
			if (get_block(inode, (sector_t)iblock, &bhs[i], 0))
				return -EIO;
		} else {
			return -EIO;
		}
		arr[nr++] = &bhs[i];
	}

	ll_rw_block (READ, nr, arr);
	kunmap (page);

	/*
	 * FIXME-VS: wait is here
	 */
	for (j = 0; j < nr; j ++) {
		if (!buffer_uptodate (arr[j]))
			break;
	}
	if (j == nr)
		SetPageUptodate (page);
	else
		ClearPageUptodate (page);
	return 0;
}


int sb_set_blocksize(struct super_block *sb, int size)
{
	sb->s_blocksize = size;
	for (sb->s_blocksize_bits = 0; size >>= 1; sb->s_blocksize_bits ++);
	return sb->s_blocksize;
}


/* fs/dcache.c */
struct dentry * d_alloc_root(struct inode * inode)
{
	struct dentry * d;
	const struct qstr root_qstr = { "/", 1, 0 };

	d = malloc (sizeof (*d));
	assert ("vs-644", d);
	memset (d, 0, sizeof (*d));
	d->d_name = root_qstr;
	d->d_inode = inode;
	return d;
}


znode *allocate_znode( reiser4_tree *tree, znode *parent,
		       unsigned int level, const reiser4_block_nr *addr )
{
	znode *root;
	int    result;

	root = zget( tree, addr, parent, level, GFP_KERNEL );

	if( znode_above_root( root ) ) {
		root -> ld_key = *min_key();
		root -> rd_key = *max_key();
		return root;
	}
	root -> nplug = node_plugin_by_id( NODE40_ID );
	result = zinit_new( root );
	assert( "nikita-1171", result == 0 );
	zrelse( root );
	return root;
}

int rseq_search( reiser4_key *array, int n, reiser4_key *key )
{
	int i;

	for( i = n - 1 ; i >= 0 ; -- i ) {
		switch( keycmp( key, &array[ i ] ) ) {
		case EQUAL_TO:
			return 1;
		case LESS_THAN:
			return 0;
		default:
		}
	}
	return 0;
}

int seq_search( reiser4_key *array, int n, reiser4_key *key )
{
	int i;

	for( i = 0 ; i < n ; ++ i ) {
		switch( keycmp( key, &array[ i ] ) ) {
		case EQUAL_TO:
			return 1;
		case GREATER_THAN:
			return 0;
		default:
		}
	}
	return 0;
}

int bin_search( reiser4_key *array, int n, reiser4_key *key )
{
	int left;
	int right;
	int found;

	/* binary search for item that can contain given key */
	left = 0;
	right = n - 1;
	found = 0;

	do {
		int             median;

		median = ( left + right ) / 2;

		switch( keycmp( key, &array[ median ] ) ) {
		case EQUAL_TO:
			if( ! REISER4_NON_UNIQUE_KEYS ) {
				left = median;
				found = 1;
			} else
				right = median;
			break;
		case LESS_THAN:
			right = median - 1;
			break;
		default:
			wrong_return_value( "nikita-1267", "keycmp" );
		case GREATER_THAN:
			left = median + 1;
			break;
		}
		if( left == right ) {
			found = keyeq( key, &array[ left ] );
			break;
		}
	} while( ( left < right ) && !found );
	return found;
}

void test_search( int rounds, int size, int num )
{
	int i;
	int j;
	int k;
	__u64 el;
	reiser4_key *key[ num ];
	u_int64_t cycles;

	for( i = 0 ; i < num ; ++ i ) {
		key[ i ] = xxmalloc( sizeof key[ i ][ 0 ] * size );
	}
	for( i = 0 ; i < num ; ++ i ) {
		for( j = 0 ; j < size ; ++ j ) {
			for( k = 0 ; k < 3 ; ++ k ) {
				el = i + j + k;
				set_key_el( &key[ i ][ j ], k, el );
			}
		}
	}
	cycles = 0;
	for( i = 0 ; i < rounds ; ++ i ) {
		reiser4_key x;
		u_int64_t stamp1;
		u_int64_t stamp2;

		for( k = 0 ; k < 3 ; ++ k ) {

			el = k + (int) ( ( ( double ) size + num + k - 2 )*rand()/(RAND_MAX+(double)k));
			set_key_el( &x, k, el );
		}
		rdtscll( stamp1 );
		rseq_search( key[ i % num ], size, &x );
		rdtscll( stamp2 );
		cycles += stamp2 - stamp1;
	}
	dinfo( "total %lu cycles, %f cycles per search\n", 
	       ( unsigned long ) cycles, ( ( double ) cycles ) / rounds );
}

static spinlock_t lc_rand_guard;

static __u64 lc_seed = 5772156648ull;
#define LC_RAND_MAX    (10000000000ull)
static __u64 lc_rand()
{
	const __u64 a = 3141592621ull;
	const __u64 c = 2718281829ull;
	const __u64 m = LC_RAND_MAX;
	spin_lock( &lc_rand_guard );
	lc_seed = ( a * lc_seed + c ) % m;
	spin_unlock( &lc_rand_guard );
	return lc_seed;
}

static __u64 lc_rand_max( __u64 max )
{
	__u64 result;

	result = lc_rand();
	result *= max;
	result /= LC_RAND_MAX;
	return result;
}

static struct inode * call_lookup (struct inode * dir, const char * name);
static int call_mkdir (struct inode * dir, const char * name);
static int call_mkdir (struct inode * dir, const char * name);
static struct inode *sandbox( struct inode * dir );

typedef struct echo_filldir_info {
	int eof;
	const char *prefix;
	int fired;
	char *name;
	int inum;
} echo_filldir_info;

typedef enum {
	EFF_SHOW_INODE      = ( 1 << 0 )
} echo_filldir_flag;

static int one_shot_filldir(void *arg, const char *name, int namelen, 
			    loff_t offset, ino_t inum, unsigned ftype)
{
	echo_filldir_info *info;

	info = arg;
	info -> eof = 0;
	
	if( info -> fired == 0 ) {
		info -> fired = 1;
		info -> name = strdup( name );
		info -> inum = ( int ) inum;
		info( "%s[%i]: %s (%i), %Lx, %lx, %i\n", info -> prefix,
		      current_pid, name, namelen, offset, inum, ftype );
		return 0;
	} else {
		info -> fired = 0;
		return -EINVAL;
	}
}

static int echo_filldir(void *arg, const char *name, int namelen, 
			loff_t offset, ino_t inum, unsigned ftype)
{
	echo_filldir_info *info;

	info = arg;
	info -> eof = 0;
	if( lc_rand_max( 10ull ) < 2 )
		return -EINVAL;
	info( "%s[%i]: %s (%i), %Lx, %lx, %i\n", info -> prefix,
	      current_pid, name, namelen, offset, inum, ftype );
	return 0;
}

static int readdir2( const char *prefix, struct file *dir, __u32 flags )
{
	echo_filldir_info info;
	int result;
		
	xmemset( &info, 0, sizeof info );
	info.prefix = prefix;

	do {
		info.eof = 1;
		result = dir -> f_dentry -> d_inode -> i_fop -> 
			readdir( dir, &info, 
				 flags ? one_shot_filldir : echo_filldir );
		if( info.eof )
			break;
		if( ( flags & EFF_SHOW_INODE ) && ( info.name != NULL ) ) {
			struct inode *i;

			if( strcmp( info.name, "." ) &&
			    strcmp( info.name, ".." ) ) {
				/*
				 * FIXME-VS: this because reiser4_iget
				 * deadlocks for "." and ".."
				 */
				i = call_lookup( dir -> f_dentry -> d_inode, info.name );
				if( IS_ERR( i ) )
					warning( "nikita-1767", "Not found: %s", 
						 info.name );
				else if( ( int ) i -> i_ino != info.inum )
					warning( "nikita-1768", 
						 "Wrong inode number: %i != %i",
						 ( int ) info.inum, ( int ) i -> i_ino );
				else
					print_inode( info.name, i );
				free( info.name );
				iput( i );
			}			
		}
	} while( !info.eof && ( result == 0 ) );

	return result;
}

typedef struct mkdir_thread_info {
	int           max;
	int           num;
	struct inode *dir;
	int           mkdir;
	int           unlink;
	int           sleep;
	int           random_p;
} mkdir_thread_info;

static int call_create (struct inode * dir, const char * name);
static ssize_t call_write (struct inode *, const char * buf,
			   loff_t offset, unsigned count);
static ssize_t call_write2 (struct inode * inode,
			    loff_t offset, unsigned count);
static ssize_t call_read (struct inode *, char * buf, 
			  loff_t offset, unsigned count);
void call_truncate (struct inode * inode, loff_t size);
static int call_readdir (struct inode * dir, const char *prefix);
static struct inode * create_root_dir (znode * root);

static int create_twig( reiser4_tree *tree, struct inode *root )
{
	int i;

	for( i = 0 ; tree -> height < TWIG_LEVEL ; ++ i ) {
		int result;
		char name[ 100 ];

		sprintf( name, "__pad-%i", i );
		result = call_create ( root, name );
		if( ( result != 0 ) && ( result != -EEXIST ) )
			return result;
	}
	info( "%i files inserted to create twig level\n", i );
	return 0;
}

static void call_umount (struct super_block * sb)
{
	fsync_bdev (sb->s_bdev);

	if (sb->s_op->put_super)
		sb->s_op->put_super (sb);

	iput (sb->s_root->d_inode);
}


static int call_unlink( struct inode * dir, struct inode *victim, 
			const char *name, int dir_p )
{
	struct dentry guillotine;
	int result;

	xmemset( &guillotine, 0, sizeof guillotine );
	guillotine.d_inode = victim;
	guillotine.d_name.name = name;
	guillotine.d_name.len = strlen( name );
	if( dir_p ) {
		result = dir -> i_op -> rmdir( dir, &guillotine );
	} else {
		truncate_inode_pages (victim->i_mapping, 0ull);
		result = dir -> i_op -> unlink( dir, &guillotine );
	}
	return result;
}

static int call_rm( struct inode * dir, const char *name )
{
	struct inode *victim;

	victim = call_lookup( dir, name );
	if( !IS_ERR( victim ) ) {
		int result;
		
		result = call_unlink( dir, victim, name, 0 );
		iput( victim );
		return result;
	} else
		return PTR_ERR( victim );
}

static int call_rmdir( struct inode * dir, const char *name )
{
	struct inode *victim;

	victim = call_lookup( dir, name );
	if( !IS_ERR( victim ) ) {
		int result;
		
		result = call_unlink( dir, victim, name, 1 );
		iput( victim );
		return result;
	} else
		return PTR_ERR( victim );
}


static int call_link( struct inode *dir, const char *old, const char *new )
{
	struct dentry old_dentry;
	struct dentry new_dentry;

	xmemset( &old_dentry, 0, sizeof old_dentry );
	xmemset( &new_dentry, 0, sizeof new_dentry );

	new_dentry.d_name.name = new;
	new_dentry.d_name.len = strlen( new );

	old_dentry.d_inode = call_lookup( dir, old );
	if( !IS_ERR( old_dentry.d_inode ) ) {
		int r;

		r = dir -> i_op -> link( &old_dentry, dir, &new_dentry );
		iput( old_dentry.d_inode );
		iput( new_dentry.d_inode );
		return r;
	} else
		return PTR_ERR( old_dentry.d_inode );
}

static int call_ln( struct inode *dir, char *cmd )
{
	char *old;
	char *new;

	old = strsep( &cmd, (char *)" " );
	new = cmd;
	return call_link( dir, old, new );
}

void *mkdir_thread( mkdir_thread_info *info )
{
	int                i;
	char               dir_name[ 30 ];
	char               name[ 30 ];
	struct dentry      dentry;
	struct inode      *f;
	int                ret;
	struct file        df;

	register_thread();

	sprintf( dir_name, "Dir-%i", current_pid );
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = dir_name;
	dentry.d_name.len = strlen( dir_name );
	ret = info -> dir -> i_op -> mkdir( info -> dir, 
					    &dentry, S_IFDIR | 0777 );
	rlog( "nikita-1638", "In directory: %s", dir_name );

	if( ret != 0 ) {
		rpanic( "nikita-1636", "Cannot create dir: %i", ret );
	}
	
	f = dentry.d_inode;
	for( i = 0 ; i < info -> num ; ++ i ) {
		__u64 fno;
		struct timespec delay;
		const char *op;
		
		if( info -> random_p )
			fno = lc_rand_max( ( __u64 ) info -> max );
		else
			fno = i;
		
//		sprintf( name, "%i", i );
		sprintf( name, "%lli-хлоп-Zzzz.", fno );
		if( info -> unlink ) {
			if( lc_rand_max( 10ull ) < 5 ) {
				op = "unlink";
				ret = call_rm( f, name );
			} else {
				op = "create";
				ret = call_create( f, name );
			}
		} else if( info -> mkdir ) {
			op = "mkdir";
			ret = call_mkdir( f, name );
		} else {
			op = "create";
			ret = call_create( f, name );
		}
		info( "(%i) %i:%s %s/%s: %i\n", current_pid, i, op,
		      dir_name, name, ret );
		if( ( ret != 0 ) && ( ret != -EEXIST ) && ( ret != -ENOENT ) &&
		    ( ret != -EINTR ) )
			rpanic( "nikita-1493", "!!!" );
		/* print_tree_rec( "tree", tree, 
		   REISER4_NODE_PRINT_ZNODE ); */
	  
		if( info -> sleep && ( lc_rand_max( 10ull ) < 2 ) ) {
			delay.tv_sec  = 0;
			delay.tv_nsec = lc_rand_max( 1000000000ull );
			nanosleep( &delay, NULL );
		}
	}
	xmemset( &df, 0, sizeof df );
	xmemset( &dentry, 0, sizeof dentry );

	call_readdir( f, dir_name );
	info( "(%i): done.\n", current_pid );
	iput( f );
	deregister_thread();
	return NULL;
}

void *mkdir_thread_start( void *arg )
{
	REISER4_ENTRY_PTR( ( ( mkdir_thread_info * ) arg ) -> dir -> i_sb );
	mkdir_thread( arg );
	REISER4_EXIT_PTR( NULL );
}

static void print_percentage( unsigned long reached, 
			      unsigned long total, int gap )
{
	int percentage;

	percentage = reached / ( ( ( double ) total ) / 100.0 );
	if( percentage * ( total / 100 ) == reached )
	{
		if( ( percentage / 10 ) * 10 == percentage )
		{
			printf( "%i%%", percentage );
		}
		else if( percentage % 2 == 0 )
		{
			printf( "%c", gap );
		}
		fflush( stdout );
	}
}

const reiser4_key ROOT_DIR_KEY = {
	.el = { { ( 2 << 4 ) | KEY_SD_MINOR }, { 42ull }, { 0ull } }
};

TS_LIST_DECLARE( mt_queue );

typedef struct mt_queue_el_s {
	int                datum;
	mt_queue_list_link linkage;
} mt_queue_el_t;

typedef struct mt_queue_s {
	int capacity;
	int elements;
	mt_queue_list_head body;
	kcond_t not_empty;
	kcond_t not_full;
	spinlock_t custodian;
	int        buried;
} mt_queue_t;

TS_LIST_DEFINE( mt_queue, mt_queue_el_t, linkage );

static int mt_queue_init( int cap, mt_queue_t *queue )
{
	mt_queue_list_init( &queue -> body );
	queue -> capacity = cap;
	queue -> elements = 0;
	queue -> buried = 0;
	kcond_init( &queue -> not_full );
	kcond_init( &queue -> not_empty );
	spin_lock_init( &queue -> custodian );
	return 0;
}

static int mt_queue_get( mt_queue_t *queue )
{
	int            v;
	mt_queue_el_t *el;

	v = 0;
	spin_lock( &queue -> custodian );
	while( queue -> elements == 0 ) {
		if( queue -> buried ) {
			spin_unlock( &queue -> custodian );
			return -ENOENT;
		}
		v = kcond_wait( &queue -> not_empty, &queue -> custodian, 1 );
	}
	if( v != 0 )
		return v;
	el = mt_queue_list_pop_front( &queue -> body ); 
	-- queue -> elements;
	spin_unlock( &queue -> custodian );
	v = el -> datum;
	kcond_signal( &queue -> not_full );
	free( el );
	return v;
}

static int mt_queue_put( mt_queue_t *queue, int value )
{
	mt_queue_el_t *el;
	int ret;

	el = xxmalloc( sizeof *el );
	if( el == NULL )
		return -ENOMEM;

	mt_queue_list_clean( el );
	el -> datum = value;

	ret = 0;
	spin_lock( &queue -> custodian );
	while( queue -> elements == queue -> capacity ) {
		if( queue -> buried ) {
			spin_unlock( &queue -> custodian );
			return -ENOENT;
		}
		ret = kcond_wait( &queue -> not_full, &queue -> custodian, 1 );
	}
	if( ret != 0 )
		return ret;
	mt_queue_list_push_back( &queue -> body, el );
	++ queue -> elements;
	spin_unlock( &queue -> custodian );
	kcond_signal( &queue -> not_empty );
	return 0;
}

static int mt_queue_bury( mt_queue_t *queue )
{
	spin_lock( &queue -> custodian );
	queue -> buried = 1;
	spin_unlock( &queue -> custodian );
	kcond_broadcast( &queue -> not_full );
	kcond_broadcast( &queue -> not_empty );
	return 0;
}

static int mt_queue_is_buried( mt_queue_t *queue )
{
	int result;

	spin_lock( &queue -> custodian );
	result = queue -> buried;
	spin_unlock( &queue -> custodian );
	return result;
}

static void mt_queue_info( mt_queue_t *queue )
{
	spin_lock( &queue -> custodian );
	assert( "nikita-1920", 
		( 0 <= queue -> elements ) &&
		( queue -> elements <= queue -> capacity ) );
	info( "queue: %i %i\n", queue -> elements, queue -> capacity );
	spin_unlock( &queue -> custodian );
}

typedef enum {
	any_role = 0,
	producer = 1,
	consumer = 2
} mt_queue_thread_role_t;

typedef struct mt_queue_thread_info {
	mt_queue_t  *queue;
	int          ops;
	struct super_block *sb;
	mt_queue_thread_role_t role;
} mt_queue_thread_info;

void *mt_queue_thread( void *arg )
{
	mt_queue_thread_info *info = arg;
	mt_queue_t  *queue;
	int i;
	int v;
	REISER4_ENTRY_PTR( info -> sb );

	queue = info -> queue;
	for( i = 0 ; 
	     !mt_queue_is_buried( queue ) && ( ( info -> role != any_role ) ||
					       ( i < info -> ops ) ) ; ++ i ) {
		mt_queue_thread_role_t role;

		if( info -> role != any_role )
			role = info -> role;
		else
			role = lc_rand_max( 2ull ) + 1;
		switch( role ) {
		case consumer:
			v = mt_queue_get( queue );
			info( "(%i) %i: got: %i\n", current_pid, i, v );
			break;
		case producer:
			v = lc_rand_max( ( __u64 ) INT_MAX );
			mt_queue_put( queue, v );
			info( "(%i) %i: put: %i\n", current_pid, i, v );
			break;
		default:
			impossible( "nikita-1917", "Revolution #9." );
			break;
		}
		mt_queue_info( queue );
	}
	info( "(%i): done.\n", current_pid );
	REISER4_EXIT_PTR( NULL );
}

int nikita_test( int argc UNUSED_ARG, char **argv UNUSED_ARG, 
		 reiser4_tree *tree )
{
	znode *root;
	znode *fake;
	int ret;
	carry_pool  pool;
	carry_level lowest_level;
	carry_op   *op;
	reiser4_key key;
	coord_t coord;
	carry_insert_data cdata;
	int i;

	assert( "nikita-1096", tree != NULL );

	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block );
	root -> rd_key = *max_key();
	sibling_list_insert( root, NULL );

	if( !strcmp( argv[ 2 ], "mkfs" ) ) {
		/*
		 * root is already allocated/initialised above.
		 */
		fs_is_here = 0;
		create_root_dir( root );
		info( "Done.\n" );
	} else if( !strcmp( argv[ 2 ], "clean" ) ) {
		ret = cut_tree( tree, min_key(), max_key() );
		printf( "result: %i\n", ret );
	} else if( !strcmp( argv[ 2 ], "print" ) ) {
		print_tree_rec( "tree", tree, (unsigned) atoi( argv[ 3 ] ) );
	} else if( !strcmp( argv[ 2 ], "load" ) ) {
	} else if( !strcmp( argv[ 2 ], "unlink" ) ) {
		struct inode *f;
		char name[ 30 ];

		f = sandbox( create_root_dir( root ) );
		create_twig( tree, f );
		call_readdir( f, "unlink-start" );
		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			sprintf( name, "%x-%x", i, i*10 );
			ret = call_create( f, name );
			assert( "nikita-1769", ret == 0 );
			print_percentage( ( ulong ) i, 
					  ( ulong ) atoi( argv[ 3 ] ), '+' );
		}
		call_readdir( f, "unlink-filled" );
		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			sprintf( name, "%x-%x", i, i*10 );
			ret = call_rm( f, name );
			assert( "nikita-1770", ret == 0 );
			print_percentage( ( ulong ) i, 
					  ( ulong ) atoi( argv[ 3 ] ), '-' );
		}
		call_readdir( f, "unlink-end" );
	} else if( !strcmp( argv[ 2 ], "dir" ) || 
		   !strcmp( argv[ 2 ], "rm" ) ||
		   !strcmp( argv[ 2 ], "mongo" ) ) {
		int threads;
		pthread_t *tid;
		mkdir_thread_info info;
		struct inode *f;

		f = sandbox( create_root_dir( root ) );
		create_twig( tree, f );
		threads = atoi( argv[ 3 ] );
		assert( "nikita-1494", threads > 0 );
		tid = xxmalloc( threads * sizeof tid[ 0 ] );
		assert( "nikita-1495", tid != NULL );

		print_inode( "inode", f );

		ret = call_create( f, "foo" );
		info( "ret: %i\n", ret );
		ret = call_create( f, "bar" );
		info( "ret: %i\n", ret );
		spin_lock_init( &lc_rand_guard );
		memset( &info, 0, sizeof info );
		info.dir = f;
		info.num = atoi( argv[ 4 ] );
		info.max = info.num;
		info.sleep = 0;
		info.random_p = atoi( argv[ 5 ] );
		if( !strcmp( argv[ 2 ], "dir" ) )
			info.mkdir = 1;
		else if( !strcmp( argv[ 2 ], "rm" ) )
			info.unlink = 1;
		if( threads > 1 ) {
			for( i = 0 ; i < threads ; ++ i )
				pthread_create( &tid[ i ], NULL, 
						mkdir_thread_start, &info );

			/*
			 * actually, there is no need to join them. Can either
			 * call thread_exit() here, or create them detached.
			 */
			for( i = 0 ; i < threads ; ++ i )
				pthread_join( tid[ i ], NULL );
		} else
			mkdir_thread( &info );

		call_readdir( f, argv[ 2 ] );
		print_tree_rec( "tree-dir", tree, REISER4_NODE_CHECK );
	} else if( !strcmp( argv[ 2 ], "queue" ) ) {
		/*
		 * a.out nikita queue T C O | egrep '^queue' | cut -f2 -d' '
		 *
		 * and feed output to gnuplot
		 */
		mt_queue_thread_info info;
		mt_queue_thread_info copy1;
		mt_queue_thread_info copy2;
		mt_queue_t queue;
		int        capacity;
		int        threads;
		int        ops;
		pthread_t *tid;

		threads  = atoi( argv[ 3 ] );
		capacity = atoi( argv[ 4 ] );
		ops      = atoi( argv[ 5 ] );

		ret = mt_queue_init( capacity, &queue );
		assert( "nikita-1918", ret == 0 ); /* Civil war */

		tid = xxmalloc( ( threads + 2 ) * sizeof tid[ 0 ] );
		assert( "nikita-1919", tid != NULL );

		for( i = 0 ; i < capacity / 2 ; ++ i )
			mt_queue_put( &queue, 
				      ( int ) lc_rand_max( ( __u64 ) INT_MAX ) );

		info.queue = &queue;
		info.ops   = ops;
		info.sb    = reiser4_get_current_sb();
		info.role  = any_role;

		for( i = 0 ; i < threads ; ++ i )
			pthread_create( &tid[ i ],
					NULL, mt_queue_thread, &info );
		copy1 = info;
		copy1.role = consumer;
		pthread_create( &tid[ threads ], 
				NULL, mt_queue_thread, &copy1 );

		copy2 = info;
		copy2.role = producer;
		pthread_create( &tid[ threads + 1 ], 
				NULL, mt_queue_thread, &copy2 );

		for( i = 0 ; i < threads ; ++ i )
			pthread_join( tid[ i ], NULL );
		mt_queue_bury( &queue );
		for( i = threads ; i < threads + 2 ; ++ i )
			pthread_join( tid[ i ], NULL );
	} else if( !strcmp( argv[ 2 ], "ibk" ) ) {
		reiser4_item_data data;
		struct {
			reiser4_stat_data_base base;
			reiser4_unix_stat      un;
		} sd;
		for( i = 0 ; ( i < atoi( argv[ 3 ] ) ) ||
			     ( tree -> height < TWIG_LEVEL ) ; ++ i ) {
			lock_handle lh;

			coord_init_zero( &coord );
			init_lh( &lh );

			info( "_____________%i_____________\n", i );
			key_init( &key );
			set_key_objectid( &key, ( __u64 ) 1000 + i * 8 );

			cputod16( S_IFREG | 0111, &sd.base.mode );
			cputod16( 0x0 , &sd.base.extmask );
			cputod32( 1, &sd.base.nlink );
			cputod64( 0x283746ull + i, &sd.base.size );
			cputod32( 201, &sd.un.uid );
			cputod32( 1, &sd.un.gid );
			cputod32( ( unsigned ) time( NULL ), &sd.un.atime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.mtime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.ctime );
			cputod64( 0x283746ull + i, &sd.un.bytes );

			data.data = ( char * ) &sd;
			data.user = 0;
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( STATIC_STAT_DATA_ID );

			ret = insert_by_key( tree, &key, &data, &coord, &lh, 
					     LEAF_LEVEL,
					     ( inter_syscall_rap * )1, 0, 
					     CBK_UNIQUE );
			printf( "result: %i\n", ret );

			/* print_pbk_cache( "pbk", tree -> pbk_cache ); */
			/* print_tree( "tree", tree ); */
			info( "____end______%i_____________\n", i );

			done_lh( &lh );

		}
		print_tree_rec( "tree:ibk", tree, REISER4_NODE_CHECK );
		/* print_tree_rec( "tree", tree, ~0u ); */
	} else if( !strcmp( argv[ 2 ], "carry" ) ) {
		reiser4_item_data data;
		struct {
			reiser4_stat_data_base base;
			reiser4_unix_stat      un;
		} sd;

		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			init_carry_pool( &pool );
			init_carry_level( &lowest_level, &pool );
		
			op = post_carry( &lowest_level, 
						 COP_INSERT, root, 0 );
			assert( "nikita-1268", !IS_ERR( op ) && ( op != NULL ) );
			// fill in remaining fields in @op, according to
			// carry.h:carry_op
			cdata.data  = &data;
			cdata.key   = &key;
			cdata.coord = &coord;
			op -> u.insert.type = COPT_ITEM_DATA;
			op -> u.insert.d = &cdata;

			xmemset( &sd, 0, sizeof sd );
			cputod16( S_IFREG | 0111, &sd.base.mode );
			cputod16( 0x0 , &sd.base.extmask );
			cputod32( 1, &sd.base.nlink );
			cputod64( 0x283746ull + i, &sd.base.size );
			cputod32( 201, &sd.un.uid );
			cputod32( 1, &sd.un.gid );
			cputod32( ( unsigned ) time( NULL ), &sd.un.atime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.mtime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.ctime );
			cputod64( 0x283746ull + i, &sd.un.bytes );

			/* this inserts stat data */
			data.data = ( char * ) &sd;
			data.user = 0;
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( STATIC_STAT_DATA_ID );
			coord_init_first_unit( &coord, NULL );
			
			set_key_locality( &key, 2ull + i );

			coord.between = ( i == 0 ) ? AT_UNIT : AFTER_UNIT;
			info( "_____________%i_____________\n", i );
			coord_print( "before", &coord, 1 );
			ret = carry( &lowest_level, NULL );
			printf( "result: %i\n", ret );
			done_carry_pool( &pool );
			coord_print( "after", &coord, 1 );
			print_znode_content( root, REISER4_NODE_PRINT_ALL );
			info( "____end______%i_____________\n", i );
		}
		print_tree_rec( "tree", tree, 0 );
	} else if( !strcmp( argv[ 2 ], "inode" ) ) {
		struct inode f;
		xmemset( &f, 0, sizeof f );
		INIT_LIST_HEAD( &f.i_hash );
		INIT_LIST_HEAD( &f.i_list );
		INIT_LIST_HEAD( &f.i_dentry );
	
		INIT_LIST_HEAD( &f.i_dirty_buffers );
		INIT_LIST_HEAD( &f.i_dirty_data_buffers );

		f.i_ino = 42;
		atomic_set( &f.i_count, 0 );
		f.i_mode = 0;
		f.i_nlink = 1;
		f.i_uid = 201;
		f.i_gid = 201;
		f.i_size = 1000;
		f.i_atime = 0;
		f.i_mtime = 0;
		f.i_ctime = 0;
		f.i_blkbits = 12;
		f.i_blksize = 4096;
		f.i_blocks = 1;
		f.i_mapping = &f.i_data;
		f.i_sb = reiser4_get_current_sb();

		init_inode( &f, &coord );
		print_inode( "inode", &f );
	} else if( !strcmp( argv[ 2 ], "cut" ) ) {
		reiser4_item_data data;
		struct {
			reiser4_stat_data_base base;
			reiser4_unix_stat      un;
		} sd;

		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			lock_handle lh;

			coord_init_zero( &coord );
			init_lh( &lh );

			info( "_____________%i_____________\n", i );
			key_init( &key );
			set_key_objectid( &key, ( __u64 ) 1000 + i * 8 );

			cputod16( S_IFREG | 0111, &sd.base.mode );
			cputod16( 0x0 , &sd.base.extmask );
			cputod32( 1, &sd.base.nlink );
			cputod64( 0x283746ull + i, &sd.base.size );
			cputod32( 201, &sd.un.uid );
			cputod32( 1, &sd.un.gid );
			cputod32( ( unsigned ) time( NULL ), &sd.un.atime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.mtime );
			cputod32( ( unsigned ) time( NULL ), &sd.un.ctime );
			cputod64( 0x283746ull + i, &sd.un.bytes );

			data.data = ( char * ) &sd;
			data.user = 0;
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( STATIC_STAT_DATA_ID );

			ret = insert_by_key( tree, &key, &data, &coord, &lh, 
					     LEAF_LEVEL,
					     ( inter_syscall_rap * )1, 0, 
					     CBK_UNIQUE );
			printf( "result: %i\n", ret );

			/* print_pbk_cache( "pbk", tree -> pbk_cache ); */
			/* print_tree( "tree", tree ); */
			info( "____end______%i_____________\n", i );

			done_lh( &lh );
		}
		ret = cut_tree( tree, min_key(), max_key() );
		printf( "result: %i\n", ret );
		print_tree_rec( "tree:cut", tree, ~0u );
	} else if( !strcmp( argv[ 2 ], "sizeof" ) ) {
		STYPE( reiser4_key );
		STYPE( reiser4_tree );
		STYPE( cbk_cache_slot );
		STYPE( cbk_cache );
		STYPE( pos_in_item );
		STYPE( coord_t );
		STYPE( reiser4_item_data );
		STYPE( reiser4_inode_info );
		STYPE( reiser4_super_info_data );
		STYPE( plugin_header );
		STYPE( file_plugin );
		STYPE( tail_plugin );
		STYPE( hash_plugin );
		STYPE( hook_plugin );
		STYPE( perm_plugin );
		STYPE( reiser4_plugin );
		STYPE( inter_syscall_rap );
		STYPE( reiser4_plugin_ops );
		STYPE( file_plugins );
		STYPE( item_header_40 );
		STYPE( reiser4_block_nr );
		STYPE( znode );
		STYPE( d16 );
		STYPE( d32 );
		STYPE( d64 );
		STYPE( d64 );
		STYPE( carry_op );
		STYPE( carry_pool );
		STYPE( carry_level );
		STYPE( carry_node );
		STYPE( reiser4_pool_header );
		STYPE( txn_handle );
		STYPE( txn_atom );
		STYPE( kcondvar_t );
		STYPE( spinlock_t );
		STYPE( zlock );
		STYPE( lock_handle );
		STYPE( coord_t );
	} else if( !strcmp( argv[ 2 ], "binseq" ) ) {
		if( argc == 4 )
			test_search( atoi( argv[ 1 ] ), 
				     atoi( argv[ 2 ] ), atoi( argv[ 3 ] ) );
		else {
			info( "Usage: %s rounds arrays size\n", argv[ 0 ] );
		}
	} else {
		info( "Huh?\n" );
	}
	zput( root );
	return 0;
}


/* insert stat data of root directory and make its inode */
static struct inode * create_root_dir (znode * root)
{
	carry_pool  pool;
	carry_level lowest_level;
	carry_op   *op;
	reiser4_inode_info *info;
	struct inode * inode;
	reiser4_item_data data;
	reiser4_key key;
	coord_t coord;
	struct {
		reiser4_stat_data_base base;
	} sd;
	int ret;
	carry_insert_data cdata;
	lock_handle lh;
	struct super_block *s;

	key_init( &key );
	set_key_type( &key, KEY_SD_MINOR );
	set_key_locality( &key, 2ull );
	set_key_objectid( &key, 42ull );
	init_lh( &lh );
	
	if( !fs_is_here ) {
		init_carry_pool( &pool );
		init_carry_level( &lowest_level, &pool );

		ret = longterm_lock_znode( &lh, root, 
					   ZNODE_WRITE_LOCK, ZNODE_LOCK_HIPRI );
		assert( "nikita-1792", ret == 0 );
		op = post_carry( &lowest_level, COP_INSERT, root, 0 );

		assert( "nikita-1269", !IS_ERR( op ) && ( op != NULL ) );
		// fill in remaining fields in @op, according to
		// carry.h:carry_op
		cdata.data = &data;
		cdata.key = &key;
		cdata.coord = &coord;
		op -> u.insert.type = COPT_ITEM_DATA;
		op -> u.insert.d = &cdata;
	
		xmemset( &sd, 0, sizeof sd );
		cputod16( S_IFDIR | 0111, &sd.base.mode );
		cputod16( 0x0 , &sd.base.extmask );
		cputod32( 1, &sd.base.nlink );
		cputod64( 0x283746ull, &sd.base.size );

		/* this inserts stat data */
		data.data = ( char * ) &sd;
		data.user = 0;
		data.length = sizeof sd.base;
		data.iplug = item_plugin_by_id( STATIC_STAT_DATA_ID );
		zload( root );
		coord_init_first_unit( &coord, root );
		zrelse( root );
	
		ret = carry( &lowest_level, NULL );
		printf( "result: %i\n", ret );
		info( "_____________sd inserted_____________\n" );
		done_carry_pool( &pool );
	} else {
		ret = coord_by_key( current_tree,
				    &key, &coord, &lh, ZNODE_READ_LOCK,
				    FIND_EXACT, LEAF_LEVEL, LEAF_LEVEL, 
				    CBK_UNIQUE );
		assert( "nikita-1933", ret == 0 );
	}

	done_lh( &lh );

	s = reiser4_get_current_sb();
	inode = reiser4_iget( s, &key );
	info = reiser4_inode_data (inode);

	if( info -> file == NULL )
		info -> file = default_file_plugin(s);
	if( info -> dir == NULL )
		info -> dir = default_dir_plugin(s);
	if( info -> sd == NULL )
		info -> sd = default_sd_plugin(s);
	if( info -> hash == NULL )
		info -> hash = default_hash_plugin(s);
	if( info -> tail == NULL )
		info -> tail = default_tail_plugin(s);
	if( info -> perm == NULL )
		info -> perm = default_perm_plugin(s);
	if( info -> dir_item == NULL )
		info -> dir_item = default_dir_item_plugin(s);
	
	call_create (inode, ".");
	s -> s_root -> d_inode = inode;
	return inode;
}


/*****************************************************************************************
				      WRITE TEST
 *****************************************************************************************/

static int insert_item (reiser4_tree * tree, reiser4_item_data * data,
			reiser4_key * key)
{
	coord_t coord;
	lock_handle lh;
	tree_level level;
	int result;
	inter_syscall_rap ra;

	coord_init_zero (&coord);
	init_lh (&lh);

	level = (item_id_by_plugin (data->iplug) == EXTENT_POINTER_ID) ? TWIG_LEVEL : LEAF_LEVEL;
	result = insert_by_key (tree, key, data, &coord, &lh,
				level, &ra, 0, 
				CBK_UNIQUE);

	done_lh (&lh);
	return result;
}

static int call_create (struct inode * dir, const char * name)
{
	struct dentry dentry;
	int ret;

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen( name );
	ret = dir->i_op -> create( dir, &dentry, S_IFREG | 0777 );

	if( ret == 0 )
		iput( dentry.d_inode );
	return ret;
}


static ssize_t call_write (struct inode * inode, const char * buf,
			   loff_t offset, unsigned count)
{
	ssize_t result;
	struct file file;
	struct dentry dentry;

	xmemset( &file, 0, sizeof file);
	file.f_dentry = &dentry;
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = inode;
	result = inode->i_fop->write (&file, buf, count, &offset);

	return result;
}


static ssize_t call_write2 (struct inode * inode,
			    loff_t offset, unsigned count)
{
	char * buf;
	ssize_t result;

	buf = malloc (count);
	if (!buf)
		return errno;
	memset (buf, '1', count);

	result = call_write (inode, buf, offset, count);
	free (buf);
	return result;
}


static ssize_t call_read (struct inode * inode, char * buf, loff_t offset,
			  unsigned count)
{
	ssize_t result;
	struct file file;
	struct dentry dentry;

	xmemset( &file, 0, sizeof file);
	file.f_dentry = &dentry;
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = inode;
	result = inode->i_fop->read (&file, buf, count, &offset);

	return result;
}


void call_truncate (struct inode * inode, loff_t size)
{
	truncate_inode_pages (inode->i_mapping, size);
	inode->i_size = size;
	inode->i_op->truncate (inode);
}


static struct inode * call_lookup (struct inode * dir, const char * name)
{
	struct dentry dentry;
	struct dentry * result;

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->lookup (dir, &dentry);

	if (result == NULL)
		return dentry.d_inode ? : ERR_PTR (-ENOENT);
	else
		return ERR_PTR (PTR_ERR (result));	
}


static struct inode * call_cd (struct inode * dir, const char * name)
{
	return call_lookup (dir, name);
}


static struct inode *sandbox( struct inode * dir )
{
	char dir_name[ 100 ];
	int ret;

	sprintf( dir_name, "sandbox-%li", ( long ) getpid() );
	ret = call_mkdir( dir, dir_name );
	assert( "nikita-1935", ret == 0 );
	return call_lookup( dir, dir_name );

}

static int call_mkdir (struct inode * dir, const char * name)
{
	struct dentry dentry;
	int result;

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->mkdir (dir, &dentry, S_IFDIR | 0777);

	if( result == 0 )
		iput( dentry.d_inode );
	return result;
}

static int call_readdir_common (struct inode * dir, const char *prefix, 
				__u32 flags)
{
	struct dentry dentry;
	struct file file;


	xmemset (&file, 0, sizeof (struct file));
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = dir;
	file.f_dentry = &dentry;
	readdir2 (prefix, &file, flags);

	return 0;
}

static int call_readdir (struct inode * dir, const char *prefix)
{
	return call_readdir_common( dir, prefix, 0 );
}

static int call_readdir_long (struct inode * dir, const char *prefix)
{
	return call_readdir_common( dir, prefix, ~0ul );
}


int alloc_extent (reiser4_tree *, coord_t *,
		  lock_handle *, void *);

#define BUFSIZE 255


/* this copies normal file @oldname to reiser4 filesystem (in directory @dir
   with name @newname) */
static int copy_file (const char * oldname, struct inode * dir,
		      const char * newname, struct stat * st,
		      int silent)
{
	int fd;
	char * buf;
	unsigned count;
	loff_t off;
	int result;
	struct inode * inode;


	result = 0;
	/* read source file */
	fd = open (oldname, O_RDONLY);
	if (fd == -1) {
		perror ("copy_file: open failed");
		return 1;
	}

	/* create a file in reiser4 */
	if (call_create (dir, newname)) {
		info ("copy_file: create failed\n");
		return 1;
	}
	/* get its inode */
	inode = call_lookup (dir, newname);
	if (IS_ERR (inode)) {
		info ("copy_file: lookup failed\n");
		return 1;
	}	

	buf = xxmalloc (BUFSIZE);
	if (!buf) {
		perror ("copy_file: xxmalloc failed");
		iput (inode);
		return 1;
	}

	count = BUFSIZE;
	off = 0;
	while (st->st_size) {
		if ((loff_t)count > st->st_size)
			count = st->st_size;
		if (read (fd, buf, count) != (ssize_t)count) {
			perror ("copy_file: read failed");
			iput (inode);
			return 1;
		}
		if (call_write (inode, buf, off, count) != (ssize_t)count) {
			info ("copy_file: write failed\n");
			iput (inode);
			return 1;
		}
		if (!silent) {
			switch (off % (BUFSIZE * 8)) {
			case 0:
				if (off)
					printf ("\b");
				printf ("-");
				break;
			case BUFSIZE * 4:
				printf ("\b-");
				break;
			case BUFSIZE * 1:
			case BUFSIZE * 5:
				printf ("\b\\");
				break;
			case BUFSIZE * 2:
			case BUFSIZE * 6:
				printf ("\b|");
				break;
			case BUFSIZE * 3:
			case BUFSIZE * 7:
				printf ("\b/");
				break;
			}
			fflush (stdout);
		}
		st->st_size -= count;
		off += count;
	}

	if (!silent)
		printf ("\b");
	iput (inode);
	close (fd);
	free (buf);
	return 0;
}


static const char * last_name (const char * full_name)
{
	const char * name;
	
	name = strrchr (full_name, '/');
	return name ? (name + 1) : full_name;
}


#if 0

static int get_depth (const char * path)
{
	int i;
	const char * slash;

	i = 1;
	for (slash = path; (slash = strchr (slash, '/')) != 0; i ++, slash ++);
	return i;
}


static int copy_dir (struct inode * dir)
{
	char * name = 0;
	size_t n = 0;
	struct stat st;
	int prefix;
	char * cwd;
	struct inode ** inodes;
	int depth;
	/*char label [10];*/
	int i;
	int dirs, files;
	char * local;


	/*
	 * no tails for all the directory
	 */
	reiser4_inode_data (dir)->tail = tail_plugin_by_id (NEVER_TAIL_ID);

	dirs = 0;
	files = 0;

	prefix = 0;
	cwd = getcwd (0, 0);
	if (!cwd) {
		perror ("copy_dir: getcwd failed");
		return 0;
	}

	depth = 1;
	inodes = (struct inode **) realloc (0, sizeof (struct inode *));
	if (!inodes) {
		perror ("realloc failed");
		return 0;
	}
	inodes [0] = dir;
	i = 0;
	while (getline (&name, &n, stdin) != -1) {
		/* remove '\n' */
		name [strlen (name) - 1] = 0;

		if (prefix == 0) {
			/*
			 * first line of find output is name of directory being
			 * find-ed */
			if (chdir (name)) {
				perror ("copy_dir: chdir failed");
				break;
			}
			if (name [strlen (name) - 1] == '/')
				prefix = strlen (name) - 1;
			else
				prefix = strlen (name);
			continue;
		}
		printf ("%s : ", name);
		local = name + prefix + 1;
		
		/* stat "source" file */
		if (!stat (local, &st)) {
			int new_depth;

			new_depth = get_depth (local) + 1;
			assert ("vs-344", new_depth > 1);

			for (i = new_depth - 1 ; i < depth ; ++ i)
				if (inodes [i])
					iput (inodes [i]);
			depth = new_depth;

			inodes = (struct inode **) realloc (inodes, sizeof (struct inode *) * depth);
			if (!inodes) {
				info ("copy_dir: realloc failed\n");
				break;
			}
			assert ("vs-471", inodes [depth - 2]);
			if (S_ISDIR (st.st_mode)) {
				printf ("DIR\n");

				if (call_mkdir (inodes [depth - 2], last_name (local))) {
					info ("copy_dir: mkdir failed\n");
					break;
				}
				inodes [depth - 1] = call_lookup (inodes [depth - 2], last_name (local));
				if (IS_ERR (inodes [depth - 1])) {
					info ("copy_dir: lookup failed\n");
					break;
				}
				dirs ++;
#if 0
				/*
				 * if parent directory has tails on - make
				 * child directory to have tail off
				 */
				if (tail_plugin_id (get_object_state (inodes [depth - 2])->tail) == NEVER_TAIL_ID)
					get_object_state (inodes [depth - 1])->tail = tail_plugin_by_id (ALWAYS_TAIL_ID);
				else
					get_object_state (inodes [depth - 1])->tail = tail_plugin_by_id (NEVER_TAIL_ID);
#endif
			} else if (S_ISREG (st.st_mode)) {
				printf ("REG\n");
				if (copy_file (local, inodes [depth - 2], last_name (local), &st)) {
					info ("copy_dir: copy_file failed\n");
					break;
				}
				inodes [depth - 1] = 0;
				files ++;
			} else
				printf ("OTHER");
		} else {
			perror ("copy_dir: stat failed");
			break;
		}
		/*
		sprintf (label, "TREE%d", i ++);
		print_tree_rec (label, tree_by_inode (dir), REISER4_NODE_PRINT_HEADER |
				REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
		*/
	}

	free (name);

	for (i = 0 ; i < depth ; ++ i) {
		if (inodes [i])
			iput (inodes [i]);
	}

	if (chdir (cwd)) {
		perror ("copy_dir: chdir failed");
		return 0;
	}
	free (cwd);

	info ("DONE: %d dirs, %d files\n", dirs, files);

	return 0;
}
#endif /* old copy_dir*/



static int bash_cp (char * real_file, struct inode * cwd, const char * name);
static int bash_cpr (struct inode * dir, const char * source)
{
	int result;
	DIR * d;
	struct dirent * dirent;
	struct stat st;
	struct inode * subdir;
	char * cwd;

	cwd = getcwd (0, 0);
	if (!cwd)
		return errno;
	
	result = chdir (source);
	if (result == -1)
		return errno;

	d = opendir (".");
	if (d == 0)
		return errno;

	result = 0;
	while ((dirent = readdir (d)) != 0) {
		if (!strcmp (dirent->d_name, ".") ||
		    !strcmp (dirent->d_name, ".."))
			continue;
		if (stat (dirent->d_name, &st) == -1) {
			result = errno;
			break;
		}
		{
			char * tmp;
			tmp = getcwd (0, 0);
			info ("%s/%s\n", tmp, dirent->d_name);
			free (tmp);
		}
		if (S_ISDIR (st.st_mode)) {
			result = call_mkdir (dir, dirent->d_name);
			if (result)
				break;
			subdir = call_lookup (dir, dirent->d_name);
			if (IS_ERR (subdir)) {
				result = PTR_ERR (subdir);
				break;
			}
			result = bash_cpr (subdir, dirent->d_name);
			if (result) {
				closedir (d);
				return result;
			}
			continue;
		}
		if (S_ISREG (st.st_mode)) {
			bash_cp (dirent->d_name, dir, dirent->d_name);
			continue;
		}
	}
	closedir (d);
	chdir (cwd);
	free (cwd);
	return result;
}



static int bash_mount (reiser4_context * context, char * cmd)
{
	struct super_block * sb;
	char *opts;
	char *file_name;

	file_name = strsep( &cmd, (char *)" " );
	opts = cmd;

	sb = call_mount (file_name, opts);
	if (IS_ERR (sb)) {
		return PTR_ERR (sb);
	}

	/* REISER4_ENTRY */
	init_context (context, sb);
	return 0;
}


static void bash_umount (reiser4_context * context)
{
	int ret;
	struct super_block * sb;
	int fd;

	sb = reiser4_get_current_sb ();
	fd = sb->s_bdev->bd_dev;
	call_umount (sb);

	close (fd);

	/* free all pages and inodes, make sure that there are no dirty/used
	 * pages/inodes */
	invalidate_inodes ();
	invalidate_pages ();

	/* REISER4_EXIT */
        ret = txn_end (context);
	done_context (context);

	/*
	txn_mgr_force_commit (s);
	*/
}




static int mkfs_bread (reiser4_tree *tree, jnode *node)
{
	struct buffer_head bh, *pbh;
	struct page *pg;

	memset (&bh, 0, sizeof (bh));

	bh.b_blocknr = node->blocknr;
	bh.b_size = tree->super->s_blocksize;
	bh.b_bdev = tree->super->s_bdev;
	pg = grab_cache_page (get_super_private (tree->super)->fake->i_mapping, 
		       (unsigned long)*jnode_get_block (node));
	assert ("vs-669", pg);
	bh.b_data = (void *) (pg + 1);
	pbh = &bh;
	ll_rw_block (READ, 1, &pbh);
	jnode_attach_page (node, pg);
	page_cache_release (pg);
	unlock_page (pg);
	kmap (pg);
	spin_lock_jnode (node);
	return 0;
}


static int mkfs_getblk (reiser4_tree *tree, jnode *node UNUSED_ARG)
{
	struct page *pg;

	pg = new_page (get_super_private (tree->super)->fake->i_mapping,
		       (unsigned long)*jnode_get_block (node));
	assert ("nikita-2049", pg);
	jnode_attach_page (node, pg);
	page_cache_release (pg);
	kmap (pg);
	spin_lock_jnode (node);
	return 0;
}


static int mkfs_brelse (reiser4_tree *tree, jnode *node)
{
#if 0
	struct buffer_head bh, *pbh;

	memset (&bh, 0, sizeof (bh));

	JF_SET (node, ZNODE_DIRTY);
	assert ("vs-670", !blocknr_is_fake (&node->blocknr) && node->blocknr);
	bh.b_blocknr = node->blocknr;
	bh.b_size = tree->super->s_blocksize;
	bh.b_bdev = tree->super->s_bdev;
	bh.b_data = jdata (node);
	pbh = &bh;
	ll_rw_block (WRITE, 1, &pbh);
	kunmap (jnode_page (node));
	JF_CLR (node, ZNODE_DIRTY);
#endif
	return 0;
}

static int mkfs_bdrop (reiser4_tree *tree UNUSED_ARG, jnode *node)
{
	assert ("nikita-2056", !JF_ISSET (node, ZNODE_DIRTY));

	spin_lock (&page_list_guard);
	list_del_init (&jnode_page (node)->list);
	spin_unlock (&page_list_guard);
	jnode_detach_page (node);
	free (jnode_page (node));
	return 0;
}


int mkfs_dirty_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	assert ("vs-692", JF_ISSET (node, ZNODE_LOADED));
	set_page_dirty (jnode_page (node));
	return 0;
}

int mkfs_clean_node( reiser4_tree *tree UNUSED_ARG, jnode *node )
{
	assert ("vs-692", JF_ISSET (node, ZNODE_LOADED));
	ClearPageDirty (jnode_page (node));
	return 0;
}

static node_operations mkfs_tops = {
	.read_node     = mkfs_bread,
	.allocate_node = mkfs_getblk,
	.delete_node   = mkfs_brelse,
	.release_node  = mkfs_brelse,
	.drop_node     = mkfs_bdrop,
	.dirty_node    = mkfs_dirty_node,
	.clean_node    = mkfs_clean_node
};

#define TEST_MKFS_ROOT_LOCALITY   (41ull)
#define TEST_MKFS_ROOT_OBJECTID   (42ull)

/* this creates reiser4 filesystem of TEST_LAYOUT_ID */
static int bash_mkfs (const char * file_name)
{
	znode * fake, * root;
	struct super_block super;
	struct block_device bd;
	struct dentry root_dentry;
	reiser4_block_nr root_block;
	reiser4_block_nr next_block;
	reiser4_tree * tree;
	int result;
	unsigned long blocksize;
	struct buffer_head * bh;
	test_disk_super_block * test_sb;

	super.u.generic_sbp = kmalloc (sizeof (reiser4_super_info_data),
				       GFP_KERNEL);
	if( super.u.generic_sbp == NULL )
		BUG();
	xmemset (super.u.generic_sbp, 0, 
		 sizeof (reiser4_super_info_data));
	super.s_op = &reiser4_super_operations;
	super.s_root = &root_dentry;
	blocksize = getenv( "REISER4_BLOCK_SIZE" ) ? 
		atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
	super.s_blocksize = blocksize;
	for (super.s_blocksize_bits = 0; blocksize >>= 1; super.s_blocksize_bits ++);
	super.s_bdev = &bd;
	super.s_bdev->bd_dev = open (file_name, O_RDWR);
	if (super.s_bdev->bd_dev == -1) {
		info ("Could not open device: %s\n", strerror (errno));
		return 1;
	}
	xmemset( &root_dentry, 0, sizeof root_dentry );

	{

		REISER4_ENTRY( &super );
		txn_mgr_init( &get_super_private (&super) -> tmgr );


		get_super_private (&super)->lplug = layout_plugin_by_id (TEST_LAYOUT_ID);



		/*  make super block */
		{
			reiser4_master_sb * master_sb;
			size_t blocksize;

			blocksize = super.s_blocksize;
			bh = sb_bread (&super, (int)(REISER4_MAGIC_OFFSET / blocksize));
			assert ("vs-654", bh);
			memset (bh->b_data, 0, blocksize);

			/* master */
			master_sb = (reiser4_master_sb *)bh->b_data;
			strncpy (master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4);
			cputod16 (TEST_LAYOUT_ID, &master_sb->disk_plugin_id);
			cputod16 (blocksize, &master_sb->blocksize);


			/* block allocator */
			root_block = bh->b_blocknr + 1;
			next_block = root_block + 1;
			get_super_private (&super)->space_plug = space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID);
			get_super_private (&super)->space_plug->
				init_allocator (get_space_allocator( &super ),
						&super, &next_block );

			/* oid allocator */
			get_super_private (&super)->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
			get_super_private (&super)->oid_plug->
				init_oid_allocator (get_oid_allocator (&super), 1ull, TEST_MKFS_ROOT_OBJECTID - 3);

			/* test layout super block */
			test_sb = (test_disk_super_block *)(bh->b_data + sizeof (*master_sb));
			strncpy (test_sb->magic, TEST_MAGIC, strlen (TEST_MAGIC));
			cputod16 (HASHED_DIR_PLUGIN_ID, &test_sb->root_dir_plugin);
			cputod16 (DEGENERATE_HASH_ID, &test_sb->root_hash_plugin);
			cputod16 (NODE40_ID, &test_sb->node_plugin);
			/* this will change on put_super in accordance to state
			 * of filesystem at that time */
			cputod64 (0ull, &test_sb->root_block);
			cputod16 (0, &test_sb->tree_height);
			cputod64 (0ull, &test_sb->new_block_nr);
			cputod64 (TEST_MKFS_ROOT_OBJECTID - 3, &test_sb->next_to_use);
			mark_buffer_dirty (bh);
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
		}

		init_formatted_fake( &super );

		/* initialize empty tree */
		tree = &get_super_private( &super ) -> tree;
		init_formatted_fake( &super );
		result = init_tree( tree, &super, &root_block,
				    1/*tree_height*/, node_plugin_by_id( NODE40_ID ),
				    /*&mkfs_tops*/ &page_cache_tops);

		fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR );
		root = allocate_znode( tree, fake, tree->height, &tree->root_block);
		root -> rd_key = *max_key();
		sibling_list_insert( root, NULL );

		zput (root);
		/*zput (fake);*/

		{
			int result;
			struct inode * fake_parent, * inode;
			reiser4_key from;
			reiser4_key to;

			reiser4_stat_data_base sd;
			reiser4_item_data insert_data;
			reiser4_key key;

			/* key */
			key_init( &key );
			set_key_type( &key, KEY_SD_MINOR );
			set_key_locality( &key, 1ull );
			set_key_objectid( &key, TEST_MKFS_ROOT_LOCALITY - 3);

			/* item body */
			xmemset( &sd, 0, sizeof sd );
			cputod16( S_IFDIR | 0111, &sd.mode );
			cputod16( 0x0 , &sd.extmask );
			cputod32( 1, &sd.nlink );
			cputod64( 0ull, &sd.size );

			/* data for insertion */
			insert_data.data = ( char * ) &sd;
			insert_data.user = 0;
			insert_data.length = sizeof (sd);
			insert_data.iplug = item_plugin_by_id (STATIC_STAT_DATA_ID);
		
			result = insert_item (tree, &insert_data, &key);
			if (result) {
				info ("insert_item failed");
				return result;
			}
			
			INIT_LIST_HEAD( &inode_hash_list );

			/* get inode of fake parent */

			fake_parent = get_new_inode (&super, 2,
						     ul_find_actor, 
						     ul_init_locked_inode,
						     &key);
			assert ("vs-621", fake_parent);
			fake_parent->i_mode = S_IFDIR;
			fake_parent->i_op = &reiser4_inode_operations;
			reiser4_inode_data (fake_parent)->dir = dir_plugin_by_id (HASHED_DIR_PLUGIN_ID);
			reiser4_inode_data (fake_parent)->file = file_plugin_by_id (DIRECTORY_FILE_PLUGIN_ID);
			reiser4_inode_data (fake_parent)->hash =
				hash_plugin_by_id (DEGENERATE_HASH_ID);
			reiser4_inode_data (fake_parent)->tail = tail_plugin_by_id (NEVER_TAIL_ID);
			reiser4_inode_data (fake_parent)->perm =
				perm_plugin_by_id (RWX_PERM_ID);
			reiser4_inode_data (fake_parent)->dir_item =
				item_plugin_by_id (REISER4_DIR_ITEM_PLUGIN);
			reiser4_inode_data (fake_parent)->sd = 
				item_plugin_by_id (STATIC_STAT_DATA_ID);

			super.s_root->d_inode = fake_parent;

			reiser4_inode_data (fake_parent)->locality_id = 1;

			/*
			 * tree contains one item only. The below is necessary
			 * to create tree of height 2 and allow mkdir to work
			 */
			call_create (fake_parent, ".");
			call_create (fake_parent, "x");
			inode = call_lookup (fake_parent, "x");
			if (!inode)
				return 1;
			result = call_write2 (inode, (loff_t)0, super.s_blocksize);
			assert ("jmacd-0000", (unsigned)result == super.s_blocksize);
			result = call_unlink (fake_parent, inode, "x", 0);
			assert ("jmacd-0001", result == 0);
			inode->i_state &= ~I_DIRTY;
			iput (inode);


			/* make a directory with objectid TEST_MKFS_ROOT_LOCALITY */
			call_mkdir (fake_parent, "root_of_root");
			inode = call_lookup (fake_parent, "root_of_root");
			if (IS_ERR (inode) || inode->i_ino != TEST_MKFS_ROOT_LOCALITY) {
				info ("lookup failed or wrong inode number\n");
				exit (1);
			}
			check_me ("vs-741", call_mkdir (inode, "root") == 0);
			
			build_sd_key (fake_parent, &from);
			fake_parent->i_state &= ~I_DIRTY;
			iput (fake_parent);
			fake_parent = inode;
			/* inode of root directory */
			inode = call_lookup (fake_parent, "root");
			if (IS_ERR (inode)) {
				info ("lookup failed\n");
				exit (1);
			}
				
			/* cut everything but root directory */
			build_sd_key (inode, &to);
			
			set_key_objectid (&to, get_key_objectid (&to) - 1);
			set_key_offset (&to, get_key_offset (max_key ()));

			result = cut_tree (tree, &from, &to);
			if (result)
				return result;

			fake_parent->i_state &= ~I_DIRTY;
			inode->i_state &= ~I_DIRTY;
			iput (fake_parent);
			super.s_root->d_inode = inode;

			get_super_private (&super)->oid_plug->
				init_oid_allocator (get_oid_allocator (&super), 1ull, (__u64)( 1 << 16 ) );

			cputod64 (reiser4_inode_data( inode ) -> locality_id, 
				  &test_sb->root_locality);
			cputod64 ((__u64)inode -> i_ino, &test_sb->root_objectid);
			/* OIDS_RESERVED---macro defines in oid.c */
			cputod64 ( (__u64)( 1 << 16 ), &test_sb->next_to_use);
			mark_buffer_dirty (bh);
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
			brelse (bh);

			call_umount (&super);
			invalidate_inodes ();
			invalidate_pages ();
		}

		/*print_tree_rec ("mkfs", tree, REISER4_NODE_PRINT_ALL);*/

		result = __REISER4_EXIT( &__context );
	}
	return result;
} /* bash_mkfs */



/*
 * @file_name is name of "normal" file. @name is name of file in reiser4 tree
 * in directory @cwd. Compare contents of file "name" and normal file. If
 * contents differ - warning is printed
 */
static int bash_diff (char * real_file, struct inode * cwd, const char * name)
{
	int fd;
	char * buf1, * buf2;
	unsigned count;
	loff_t off;
	struct inode * inode;
	struct stat st;


	if (stat (real_file, &st)) {
		perror ("diff: stat failed");
		return 0;
	}

	/*
	 * open file in "normal" filesystem
	 */
	fd = open (real_file, O_RDONLY);
	if (fd == -1) {
		perror ("diff: open failed");
		return 0;
	}

	/*
	 * lookup for the file in current directory in reiser4 tree
	 */
	inode = call_lookup (cwd, name);
	if (IS_ERR (inode)) {
		info ("diff: lookup failed\n");
		return 0;
	}
	
	buf1 = xxmalloc (BUFSIZE);
	buf2 = xxmalloc (BUFSIZE);
	if (!buf1 || !buf2) {
		perror ("diff: xxmalloc failed");
		iput (inode);
		return 0;
	}

	count = BUFSIZE;
	off = 0;
	while (st.st_size) {
		if ((loff_t)count > st.st_size)
			count = st.st_size;
		if (read (fd, buf1, count) != (ssize_t)count) {
			perror ("diff: read failed");
			break;
		}
		if (call_read (inode, buf2, off, count) != (ssize_t)count) {
			info ("diff: read failed\n");
			break;
		}
		if (memcmp (buf1, buf2, count)) {
			info ("diff: files differ\n");
			break;
		}

		st.st_size -= count;
		off += count;
	}

	close (fd);
	free (buf1);
	free (buf2);
	iput (inode);
	return 0;
}


/* copy file into current directory */
static int bash_cp (char * real_file, struct inode * cwd, const char * name)
{
	struct stat st;
	int silent;
	
	if (stat (real_file, &st) || !S_ISREG (st.st_mode)) {
		errno ? perror ("stat failed") : 
			info ("%s is not regular file\n", real_file);
	}
	silent = 1;
	if (copy_file (real_file, cwd, name, &st, silent)) {
		info ("cp: copy_file failed\n");
	}
	return 0;
}




/* read content of file. name must be "name N M" */
static int bash_read (struct inode * dir, const char * name)
{
	unsigned from, count;
	char * args;
	struct inode * inode;
	char * buf;
	unsigned i;

	args = strchr (name, ' ');
	if (!args) {
		info ("usage: read name N M\n");
		return 0;
	}

	*args ++ = 0;

	if (sscanf (args, "%d %d", &from, &count) != 2) {
		info ("usage: read name N M\n");
		return 0;
	}
	info ("reading file %s from %d %d bytes\n", name, from, count);

	inode = call_lookup (dir, name);
	if (IS_ERR (inode)) {
		info ("read: lookup failed\n");
		return 0;
	}
	
	buf = xxmalloc (count);
	if (!buf) {
		info ("read: xxmalloc failed\n");
		return 0;
	}
	if (call_read (inode, buf, (loff_t)from, count) != (ssize_t)count) {
		info ("read: read failed\n");
		return 0;
	}
	info ("################### start ####################\n");
	for (i = 0; i < count; i ++)
		printf ("%c", buf[i]);
	info ("################### end ####################\n");
	iput (inode);
	return 0;
}

/* write to file. name must be "name from" */
static int bash_write (struct inode * dir, const char * name)
{
	unsigned from;
	char * args;
	struct inode * inode;
	char * buf;
	int count;
	int n;


	args = strchr (name, ' ');
	if (!args) {
		info ("usage: write name from\n");
		return 0;
	}

	*args ++ = 0;

	if (sscanf (args, "%d", &from) != 1) {
		info ("usage: write name from\n");
		return 0;
	}
	info ("writing file %s from %d\nType data (ctrl-d to end):\n", name, from);

	buf = 0;
	n = 0;
	count = getdelim (&buf, &n, '#', stdin);
	if (count == -1) {
		info ("write: getdelim failed\n");
		return 0;
	}
	inode = call_lookup (dir, name);
	if (IS_ERR (inode)) {
		info ("write: lookup failed\n");
		return 0;
	}
	if (call_write (inode, buf, (loff_t)from, (unsigned)count) != (ssize_t)count) {
		info ("write failed\n");
		return 0;
	}

	iput (inode);
	free (buf);
	return 0;
}

static void bash_df (struct inode * cwd)
{
	struct statfs st;

	cwd -> i_sb -> s_op -> statfs( cwd -> i_sb, &st );
	info( "\n\tf_type: %lx", st.f_type );
	info( "\n\tf_bsize: %li", st.f_bsize );
	info( "\n\tf_blocks: %li", st.f_blocks );
	info( "\n\tf_bfree: %li", st.f_bfree );
	info( "\n\tf_bavail: %li", st.f_bavail );
	info( "\n\tf_files: %li", st.f_files );
	info( "\n\tf_ffree: %li", st.f_ffree );
	info( "\n\tf_fsid: %lx", st.f_fsid );
	info( "\n\tf_namelen: %li\n", st.f_namelen );
}

static int bash_trace (struct inode * cwd, const char * cmd)
{
	__u32 flags;

	if( sscanf( cmd, "%i", &flags ) != 1 ) {
		info( "usage: trace N\n" );
		return 0;
	}
	get_super_private( cwd -> i_sb ) -> trace_flags = flags;
	return 0;
}

static int bash_trunc (struct inode * cwd, const char * name)
{
	struct inode * inode;
	char * args;
	loff_t new_size;


	args = strchr (name, ' ');
	if (!args) {
		info ("usage: trunc name newsize\n");
		return 0;
	}

	*args ++ = 0;

	if (sscanf (args, "%Ld", &new_size) != 1) {
		info ("usage: trunc name newsize\n");
		return 0;
	}

	inode = call_lookup (cwd, name);
	if (IS_ERR (inode)) {
		info ("could not find file %s\n",
		      name);
		return 0;
	}

	info ("Current size: %Ld, new size: %Ld\n",
	      inode->i_size, new_size);

	call_truncate (inode, new_size);
	iput (inode);
	return 0;
}

/*
 * go through all "twig" nodes and call alloc_extent for every item
 */
static void allocate_unallocated (reiser4_tree * tree)
{
/* comment this code -Hans */
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	int result;


	coord_init_zero (&coord);
	init_lh (&lh);

	key_init (&key);
	set_key_locality (&key, 2ull);
	set_key_objectid (&key, 0x2aull);
	set_key_type (&key, KEY_SD_MINOR);
	set_key_offset (&key, 0ull);
	result = coord_by_key (tree, &key, &coord, &lh,
			       ZNODE_WRITE_LOCK, FIND_MAX_NOT_MORE_THAN,
			       TWIG_LEVEL, TWIG_LEVEL, 
			       CBK_FOR_INSERT | CBK_UNIQUE);
	coord_init_first_unit (&coord, NULL);
	result = iterate_tree (tree, &coord, &lh, 
				       alloc_extent, 0, ZNODE_WRITE_LOCK, 0);

	done_lh (&lh);

	print_tree_rec ("AFTER ALLOCATION", tree, REISER4_NODE_PRINT_HEADER |
			REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
}


static int do_twig_squeeze (reiser4_tree * tree, coord_t * coord,
			    lock_handle * lh, void * arg UNUSED_ARG)
{
	lock_handle right_lock;
	int result;
	reiser4_block_nr da;
	reiser4_blocknr_hint preceder;


	assert ("vs-461", coord->item_pos == 0 && coord->unit_pos == 0 &&
		coord->between == AT_UNIT);
	/*
	 * allocate extents
	 */
	do {
		alloc_extent (tree, coord, lh, 0);
	} while (coord_next_item (coord));


	/*
	 * get preceder
	 */
	coord_init_last_unit (coord, 0);
	if (item_is_internal (coord)) {
		item_plugin_by_coord (coord)->s.internal.down_link (coord, 0,
								    &da);
		preceder.blk = da;
	} else if (item_id_by_coord (coord) == EXTENT_POINTER_ID) {
		reiser4_extent * ext;
		ext = extent_by_coord (coord);
		preceder.blk = extent_get_start (ext) + extent_get_width (ext);
	} else
		impossible ("vs-462", "unknown item type");


 get_right_neighbor:
	/*
	 * squeeze right neighbor
	 */
	init_lh (&right_lock);
	result = reiser4_get_right_neighbor (&right_lock, coord->node,
					     ZNODE_WRITE_LOCK, GN_DO_READ);
	if (result) {
		return result;
	}

	while ((result = squalloc_right_neighbor (coord->node, right_lock.node,
						  &preceder)) == SUBTREE_MOVED);
	done_lh (&right_lock);
	if (result == SQUEEZE_SOURCE_EMPTY) {
		/*
		 * get next right neighbor
		 */
		goto get_right_neighbor;
	}

	/*
	 * we have done with this node
	 */
	coord_init_last_unit (coord, coord->node);
	return 1;
}

/*
 * go through all "twig" nodes and call squeeze_right_neighbor
 */
static void squeeze_twig_level (reiser4_tree * tree)
{
	coord_t coord;
	lock_handle lh;
	reiser4_key key;
	int result;


	coord_init_zero (&coord);
	init_lh (&lh);

	key_init (&key);
	set_key_locality (&key, 2ull);
	set_key_objectid (&key, 0x2aull);
	set_key_type (&key, KEY_SD_MINOR);
	set_key_offset (&key, 0ull);
	result = coord_by_key (tree, &key, &coord, &lh,
			       ZNODE_WRITE_LOCK, FIND_MAX_NOT_MORE_THAN,
			       TWIG_LEVEL, TWIG_LEVEL, 
			       CBK_FOR_INSERT | CBK_UNIQUE);
	coord_init_first_unit (&coord, NULL);
	result = iterate_tree (tree, &coord, &lh, 
				       do_twig_squeeze, 0, ZNODE_WRITE_LOCK, 0/* through items */);

	done_lh (&lh);

	print_tree_rec ("AFTER SQUEEZING TWIG", tree, REISER4_NODE_PRINT_HEADER |
			REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
}

#define print_help() \
	info ("Commands:\n"\
	      "\tmkfs devname\n"\
	      "\tmount devname\n"\
	      "\tumount\n"\
	      "\tls             - list directory\n"\
	      "\tll             - list directory, long format\n"\
	      "\tcd             - change directory\n"\
	      "\tmkdir dirname  - create new directory\n"\
	      "\tcp filename    - copy file to current directory\n"\
              "\tcp-r dir       - copy directory recursively"\
	      "\tdiff filename  - compare files\n"\
	      "\ttrunc filename size - truncate file\n"\
	      "\ttouch          - create empty file\n"\
	      "\tread filename from count\n"\
	      "\twrite filename from\n"\
	      "\trm             - remove file\n"\
	      "\talloc          - allocate unallocated extents\n"\
	      "\tsqueeze        - squeeze twig level\n"\
	      "\ttail [on|off]  - set or get state of tail plugin of current directory\n"\
	      "\tp              - print tree\n"\
	      "\tinfo           - print fs info (height, root, etc)"\
	      "\texit\n");

static int bash_test (int argc UNUSED_ARG, char **argv UNUSED_ARG, 
		      reiser4_tree *tree UNUSED_ARG)
{
	char * command = 0;
	struct inode * cwd;
	reiser4_context context;
	int mounted;
	int result;


	mounted = 0;

	INIT_LIST_HEAD( &inode_hash_list );
	INIT_LIST_HEAD( &page_list );
	set_current ();
	/* module_init () -> reiser4_init () -> register_filesystem */
	run_init_reiser4 ();



/* for (cwd, command + strlen ("foocmd ")) 
   like
   static int bash_read (struct inode * dir, const char * name);
   static int bash_write (struct inode * dir, const char * name);
   etc
*/
#define BASH_CMD( name, function )						\
		if (!strncmp (command, (name), strlen (name))) {		\
			int code;						\
			code = (function) (cwd, command + strlen (name));	\
			if (code) {						\
				info ("%s failed: %s(%i)\n", 			\
				      command, strerror(-code), code);		\
			}							\
			continue;						\
		}

/* for (command + strlen ("foocmd "), cwd, last_name (command + strlen ("foocmd "))
   like
   static int bash_cp (char * real_file, struct inode * cwd, const char * name);
   static int bash_diff (char * real_file, struct inode * cwd, const char * name);
   etc
 */
#define BASH_CMD3( name, function )						\
		if (!strncmp (command, (name), strlen (name))) {		\
			int code;						\
			code = (function) (command + strlen (name), cwd,        \
                                          last_name (command + strlen (name)) );\
			if (code) {						\
				info ("%s failed: %i\n", command, code);	\
			}							\
			continue;						\
		}

	cwd = 0;

	for ( ; (command = readline ("> ")) != NULL ; free (command)) {
		add_history (command);

		if (!strncmp (command, "mount ", 6)) {
			if (mounted) {
				info ("Umount first\n");
				continue;
			}
			result = bash_mount (&context, command + 6);
			if (result) {
				info ("mount failed: %s\n", strerror (result));
				continue;
			}

			cwd = reiser4_get_current_sb ()->s_root->d_inode;
			mounted = 1;
			continue;
		}
		if (!strcmp (command, "umount")) {
			if (!mounted) {
				info ("Mount first\n");
				continue;
			}
			bash_umount (&context);
			mounted = 0;
			continue;
		}
		if (!strncmp (command, "mkfs ", 5)) {
			if (mounted) {
				info ("Umount first\n");
				continue;
			}
			bash_mkfs (command + 5);
			continue;
		}
		if (!strcmp (command, "exit")) {
			if (mounted)
				bash_umount (&context);
			break;
		}
		if (command[0] == 0 || !strcmp (command, "help")) {
			print_help ();
			continue;
		}
		if (!mounted) {
			info ("Mount first\n");
			continue;
		}

		if (!strncmp (command, "pwd", 2)) {
			info ("Not ready\n");
		} else if (!strncmp (command, "cd ", 3)) {
			/*
			 * cd
			 */
			struct inode * tmp;

			tmp = call_cd (cwd, command + 3);
			if (IS_ERR(tmp)) {
				info ("%s failed\n", command);
			} else
				cwd = tmp;
			continue;
		}
		BASH_CMD ("mkdir ", call_mkdir);
		BASH_CMD ("touch ", call_create);
		BASH_CMD ("rm ", call_rm);
		BASH_CMD ("rmdir ", call_rmdir);
		BASH_CMD ("ls", call_readdir);
		BASH_CMD ("ll", call_readdir_long);
		BASH_CMD ("ln ", call_ln);
		BASH_CMD ("read ", bash_read);
		BASH_CMD ("write ", bash_write);
		BASH_CMD ("trunc ", bash_trunc);
		BASH_CMD ("cp-r ", bash_cpr);
		BASH_CMD ("trace ", bash_trace);

		BASH_CMD3 ("cp ", bash_cp);
		BASH_CMD3 ("diff ", bash_diff);

		if (!strncmp (command, "tail", 4)) {
			/*
			 * get tail plugin or set
			 */
			if (!strcmp (command, "tail")) {
				print_plugin("", 
					     tail_plugin_to_plugin(inode_tail_plugin (cwd)));
			} else if (!strcmp (command + 5, "off")) {
				reiser4_inode_data (cwd) -> tail =
					tail_plugin_by_id (NEVER_TAIL_ID);
			} else if (!strcmp (command + 5, "on")) {
				reiser4_inode_data (cwd) -> tail =
					tail_plugin_by_id (ALWAYS_TAIL_ID);
			} else {
				info ("\ttail [on|off]\n");
			}
		} else if (!strcmp (command, "alloc")) {
			allocate_unallocated (tree_by_inode (cwd));
		} else if (!strcmp (command, "df")) {
			bash_df (cwd);
		} else if (!strcmp (command, "squeeze")) {
			squeeze_twig_level (tree_by_inode (cwd));
		} else if (!strncmp (command, "p", 1)) {
			/*
			 * print tree
			 */
			print_tree_rec ("DONE", tree_by_inode (cwd),
					REISER4_NODE_PRINT_ALL & ~REISER4_NODE_PRINT_PLUGINS & ~REISER4_NODE_PRINT_ZNODE);
		} else if (!strncmp (command, "info", 1)) {
			get_current_super_private ()->lplug->print_info (reiser4_get_current_sb ());
		} else
			print_help ();
	}
	info ("Done\n");
	exit (0);
}



/*****************************************************************************************
				      DEADLOCK TEST
 *****************************************************************************************/

static struct drand48_data  _rand_data;
static spinlock_t           _rand_lock;

void
sys_rand_init (void)
{
  spin_lock_init (& _rand_lock);

  srand48_r (0x482383, & _rand_data);
}

__u32
sys_lrand (__u32 max)
{
	__u32 d;
	spin_lock   (&_rand_lock);
	lrand48_r   (& _rand_data, (long*) & d);
	spin_unlock (& _rand_lock);
	return d % max;
}

typedef struct {
	reiser4_stat_data_base base;
	reiser4_unix_stat      un;
} jmacd_sd;

static __u32       _jmacd_items = 0;
static __u32       _jmacd_ops = 0;
static __u32       _jmacd_ops_done = 0;
static __u32       _jmacd_items_created = 0;
static spinlock_t  _jmacd_items_created_lock;
static spinlock_t  _jmacd_exists_lock;
static __u8*       _jmacd_exists_map;

void jmacd_key_no (reiser4_key *key, reiser4_key *next_key, jmacd_sd *sd, reiser4_item_data *id, __u64 key_no)
{
	key_no *= 8;
	key_no += 1000;
	key_init           (key);
	set_key_objectid   (key, key_no);
	set_key_locality   (key, ~0ull - getpid());

	if (next_key != NULL) {
		key_init           (next_key);
		set_key_objectid   (next_key, key_no + 1);
		set_key_locality   (next_key, ~0ull - getpid());
	}

	cputod16( 0, &sd->base.mode );
	cputod16( 0, &sd->base.extmask );
	cputod32( 1, &sd->base.nlink );
	cputod64( 0ULL, &sd->base.size );
	cputod32( 0, &sd->un.uid );
	cputod32( 0, &sd->un.gid );
	cputod32( 0, &sd->un.atime );
	cputod32( 0, &sd->un.mtime );
	cputod32( 0, &sd->un.ctime );
	cputod64( 0ULL, &sd->un.bytes );
	
	id->data = ( char * ) sd;
	id->user = 0;
	id->length = sizeof (sd->base);
	id->iplug = item_plugin_by_id( STATIC_STAT_DATA_ID );
}

void* monitor_test_handler (void* arg)
{
	struct super_block *super = (struct super_block*) arg;

	REISER4_ENTRY_PTR (super);

	for (;;) {
		sleep (10);

		show_context (0);
	}

	REISER4_EXIT_PTR (NULL);
}

void* build_test_handler (void* arg)
{
	struct super_block *super = (struct super_block*) arg;
	int ret;

	for (;;) {
		reiser4_item_data      data;
		lock_handle    lock;
		jmacd_sd               sd;
		coord_t            coord;
		reiser4_key            key;
		__u32                  count;
		reiser4_tree          *tree;
		
		REISER4_ENTRY_PTR (super);

		tree = & get_super_private (super)->tree;
		
		spin_lock (& _jmacd_items_created_lock);
		if (_jmacd_items_created == _jmacd_items) {
			spin_unlock      (& _jmacd_items_created_lock);
			REISER4_EXIT_PTR (NULL);
		}
		count = (__u64) _jmacd_items_created ++;

		/* least-key issues... first insert must be serialized. */
		if (count != 0) {
			spin_unlock (& _jmacd_items_created_lock);
		}

		/*if ((count % 100) == 0) {*/
		info ("_____________%u_____________ thread %u\n", count, (unsigned) pthread_self ());
		/*}*/

		coord_init_zero (& coord);
		jmacd_key_no (& key, NULL, & sd, & data, (__u64) count);

	deadlk:
		init_lh    (& lock);
		
		ret = insert_by_key( tree, &key, &data, &coord, &lock, 
				     LEAF_LEVEL,
				     ( inter_syscall_rap * )1, 0, CBK_UNIQUE );

		done_lh (& lock);

		/* least-key issues... first insert must be serialized. */
		if (count == 0) {
			spin_unlock (& _jmacd_items_created_lock);
		}
		
		if (ret == -EDEADLK) {
			goto deadlk;
		}

		if (ret != 0) {
			rpanic ("jmacd-1030", "build insert_by_key failed");
		}

		if ((ret = __REISER4_EXIT( &__context )) != 0) {
			rpanic ("jmacd-563", "reiser4_exit failed");
		}
	}

	return NULL;
}

void* drive_test_handler (void* arg)
{
	struct super_block *super = (struct super_block*) arg;
	int ret;

	for (;;) {
		reiser4_item_data      data;
		lock_handle    lock;
		jmacd_sd               sd;
		coord_t             coord;
		reiser4_key            key, next_key;
		reiser4_tree          *tree;
		__u32                  item;
		__u32                  exists;
		__u32                  opc;
		
		REISER4_ENTRY_PTR (super);

		tree = & get_super_private (super)->tree;
	again:
		item = sys_lrand (_jmacd_items);

		if (item == 0) {
			goto again;
		}

		spin_lock (& _jmacd_exists_lock);
		if (_jmacd_ops_done == _jmacd_ops) {
			spin_unlock (& _jmacd_exists_lock);
			REISER4_EXIT_PTR (NULL);
		}

		if ((exists = _jmacd_exists_map[item]) == 0xff) {
			spin_unlock (& _jmacd_exists_lock);
			goto again;
		}

		opc = _jmacd_ops_done ++;
		_jmacd_exists_map[item] = 0xff;
		spin_unlock (& _jmacd_exists_lock);

		/*if ((opc % 100) == 0) {*/
		info ("_____________%u_____________ %s thread %u\n", opc, exists ? "delete" : "insert", (unsigned) pthread_self ());
		/*}*/

		coord_init_zero (& coord);
		jmacd_key_no       (& key, & next_key, & sd, & data, (__u64) item);

	deadlk:
		if (exists == 0) {

			init_lh (& lock);

			ret = insert_by_key( tree, &key, &data, &coord, &lock, 
					     LEAF_LEVEL,
					     ( inter_syscall_rap * )1, 0, CBK_UNIQUE );

			done_lh (& lock);

		} else {

			ret = cut_tree (tree, & key, & next_key);
		}

		if (ret == -EDEADLK) {
			goto deadlk;
		}

		if (ret != 0) {
			rpanic ("jmacd-1032", "drive cut_tree failed");
		}

		spin_lock (& _jmacd_exists_lock);
		_jmacd_exists_map[item] = ! exists;
		spin_unlock (& _jmacd_exists_lock);
		
		if ((ret = __REISER4_EXIT( &__context )) != 0) {
			rpanic ("jmacd-563", "reiser4_exit failed");
		}
	}

	return NULL;
}

int jmacd_test( int argc UNUSED_ARG,
		char **argv UNUSED_ARG, 
		reiser4_tree *tree )
{
	__u32 i;
	struct super_block *super = reiser4_get_current_sb ();
	__u32               procs;
	pthread_t          *proc_ids;
	pthread_t           mon_id;
	znode *fake;
	znode *root;

	spin_lock_init (& _jmacd_items_created_lock);
	spin_lock_init (& _jmacd_exists_lock);

	if (argc != 6) {
	oops:
		printf ("usage: a.out jmacd command thread# item# op#\n");
		abort ();
	}

	if (strcmp (argv[2], "build") == 0) {
	} else {
		goto oops;
	}

	procs        = atoi (argv[3]);
	_jmacd_items = atoi (argv[4]);
	_jmacd_ops   = atoi (argv[5]);

	proc_ids              = xxmalloc (sizeof (pthread_t) * procs);
	_jmacd_exists_map     = xxmalloc (_jmacd_items);

	for (i = 0; i < _jmacd_items; i += 1) {
		_jmacd_exists_map[i] = 1;
	}

	/* These four magic lines are taken from nikita_test, and seem to be
	 * necessary--maybe they belong somewhere else... */
	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block );
	root -> rd_key = *max_key();
	sibling_list_insert( root, NULL );

	info ("building tree containing %u items using %u threads\n", _jmacd_items, procs);

	for (i = 0; i < procs; i += 1) {
		pthread_create (& proc_ids[i], NULL, build_test_handler, super);
	}

	/*pthread_create (& mon_id, NULL, monitor_test_handler, super);*/
	(void)mon_id;
	
	for (i = 0; i < procs; i += 1) {
		pthread_join (proc_ids[i], NULL);
	}

	info ("finished building tree containing %u items\n", _jmacd_items);

	info ("running insert/delete test for %u operations\n", _jmacd_ops);

	for (i = 0; i < procs; i += 1) {
		pthread_create (& proc_ids[i], NULL, drive_test_handler, super);
	}

	for (i = 0; i < procs; i += 1) {
		pthread_join (proc_ids[i], NULL);
	}

	return 0;
}

/*****************************************************************************************
 *                                      BITMAP TEST
 *****************************************************************************************/

#define BLOCK_COUNT 14000

/* tree op. read node which emulates read from valid reiser4 volume  */
static int bm_test_read_node (reiser4_tree * tree, jnode * node )
{
	bmap_nr_t bmap_nr;
	struct page * page;
	struct super_block * super;

	unsigned long page_idx;

	assert ("zam-413", tree != NULL);
	assert ("zam-431", node != NULL);

	super = tree -> super;

	page_idx = (unsigned long)node;

	page = grab_cache_page (get_super_fake(super)->i_mapping, page_idx);

	if (page == NULL) return -ENOMEM;
	
	jnode_attach_page (node, page);

	unlock_page (page);

	page_cache_release (page);

	spin_lock_jnode (node);

	kmap(page);

	if(!JF_ISSET(node, ZNODE_KMAPPED))
		JF_SET(node, ZNODE_KMAPPED);
	else
		kunmap( page );

	spin_unlock_jnode (node);

	if (PageUptodate(page)) return 0;

	xmemset (jdata(node), 0, tree->super->s_blocksize);

	if (! blocknr_is_fake(jnode_get_block (node))) {
		reiser4_block_nr bmap_block_addr;

		/* it is a hack for finding what block we read (bitmap block or not) */
		bmap_nr = *jnode_get_block(node) / (tree->super->s_blocksize * 8);
		get_bitmap_blocknr (tree->super,  bmap_nr, &bmap_block_addr);

		if (disk_addr_eq (jnode_get_block (node), &bmap_block_addr)) {
			int offset = *jnode_get_block(node) - (bmap_nr << super->s_blocksize_bits);
			set_bit(offset, jdata(node));

		} else {
			warning ("zam-411", "bitmap test should not read"
				 " not bitmap block #%llu", *jnode_get_block(node));

			return -EIO;
		}
	}

	SetPageUptodate(page);

	return 0;
}

/** a temporary solutions for setting up reiser4 super block */
static void fill_sb (struct super_block * super)
{
	reiser4_super_info_data * info_data = get_super_private (super);
	
	info_data -> block_count = BLOCK_COUNT;
	info_data -> blocks_used  = BLOCK_COUNT / 10;
	info_data -> blocks_free  = info_data -> block_count - info_data -> blocks_used;

	info_data -> blocks_free_committed = info_data -> blocks_free;

	/* set an allocator plugin field to bitmap-based allocator */
	info_data ->space_plug = &space_plugins[BITMAP_SPACE_ALLOCATOR_ID].space_allocator;
}

static int bitmap_test (int argc UNUSED_ARG, char ** argv UNUSED_ARG, reiser4_tree * tree)
{
	struct super_block * super = reiser4_get_current_sb();

	assert ("vs-510", get_super_private (super) != NULL);

	/* just a setting of all sb fields when real read_super is not ready */ 
	fill_sb (super);

	/*
	assert ("vs-511", get_super_private (super)->space_plug != NULL);
	*/
	if (get_super_private (super)->space_plug->init_allocator)
		get_super_private (super)->space_plug->init_allocator (
			get_space_allocator (super), super, 0);

	tree -> ops -> read_node = bm_test_read_node;


	{
		reiser4_blocknr_hint hint;

		reiser4_block_nr block;
		reiser4_block_nr len;
		
		int ret;
		int count = 0;
		int total = 0;
		
		while (1) {
			len = 30;

			blocknr_hint_init (&hint);

			//	hint.not_counted = 1;

			ret = reiser4_alloc_blocks (&hint, &block, &len);

			blocknr_hint_done (&hint);

			if (ret != 0) break;

			++ count;
			total += (int)len;

			printf ("allocated %d blocks in attempt #%d, total = %d\n", (int)len, (int)count, (int)total);

		}

		printf ("total %d blocks allocated until %d error (%s) returned\n", total, ret, strerror(-ret));
	}


	if (get_super_private (super)->space_plug->destroy_allocator)
		get_super_private (super)->space_plug->destroy_allocator (
			get_space_allocator (super), super);

	return 0;
}

static int zam_test (int argc, char ** argv, reiser4_tree * tree)
{
	char * testname;

	if (argc < 3) {
		printf ("Usage: %s zam testname ...\n", __prog_name);
		return -1;
	}

	testname = argv[2];

	{ /* eliminate already parsed command line arguments */
		int i;

		for (i = 3; i < argc; i++) {
			argv[i - 2] = argv[i];
		}
	}

	if (!strcmp(testname, "bitmap")) {
		return bitmap_test(argc - 2, argv, tree);
	}

	printf ("%s: unknown zam\'s test name\n", __prog_name);
	return 0;
}

kcond_t memory_pressed;
spinlock_t mp_guard;
int is_mp;

static void *uswapd( void *untyped )
{
	struct super_block *super = untyped;
	REISER4_ENTRY_PTR( super );

	while( 1 ) {
		int result;


		spin_lock( &mp_guard );
		while( ! is_mp )
			kcond_wait( &memory_pressed, &mp_guard, 0 );
		is_mp = 0;
		spin_unlock( &mp_guard );
		rlog( "nikita-1939", "uswapd wakes up..." );

		result = memory_pressure( super );
		if( result != 0 )
			warning( "nikita-1937", "flushing failed: %i", result );
	}
	REISER4_EXIT_PTR( NULL );
}

void declare_memory_pressure( void )
{
	return;
	spin_lock( &mp_guard );
	is_mp = 1;
	spin_unlock( &mp_guard );
	kcond_broadcast( &memory_pressed );
	rlog( "nikita-1940", "Memory pressure declared: %lli", total_allocations );
	total_allocations = 0;
}

/*****************************************************************************************
				      
 *****************************************************************************************/

typedef struct {
	const char *name;
	int ( * func )( int argc, char **argv, reiser4_tree *tree );
} tester;

static tester team[] = {
	{
		.name = "sh",
		.func = bash_test
	},
	{
		.name = "nikita",
		.func = nikita_test
	},
	{
		.name = "jmacd",
		.func = jmacd_test
	},
	{
		.name = "zam",
		.func = zam_test
	},
	{
		.name = NULL,
		.func = NULL
	},
};

extern int init_inodecache( void );

void funJustBeforeMain()
{}

/* fixme: not used */
reiser4_block_nr new_block_nr;

int PAGE_CACHE_SHIFT;
int PAGE_CACHE_SIZE;
int PAGE_CACHE_MASK;

int real_main( int argc, char **argv )
{
	int result, eresult, fresult;
	struct super_block *s;
	reiser4_tree *tree;
	reiser4_block_nr root_block;
	oid_t next_to_use;
	oid_t locality, objectid; /* key of root directory read from meta file */
	int tree_height;
	struct super_block super;
	struct dentry root_dentry;
	reiser4_context __context;
	char *mmap_fname;
	char *mmap_meta_fname;
	int   mmap_meta_fd;
	reiser4_key root_dir_key;


	__prog_name = strrchr( argv[ 0 ], '/' );
	if( __prog_name == NULL )
		__prog_name = argv[ 0 ];
	else
		++ __prog_name;
 	abendInit( argc, argv );

	/*
	 * currently reiser4 supports only pagesize==blocksize, we have to make
	 * sure that PAGE_CACHE_* are set correspondingly to blocksize
	 */
	{
		int blocksize;

		blocksize = getenv( "REISER4_BLOCK_SIZE" ) ? 
			atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
		for (PAGE_CACHE_SHIFT = 0; blocksize >>= 1; PAGE_CACHE_SHIFT ++);
		
		PAGE_CACHE_SIZE	= (1UL << PAGE_CACHE_SHIFT);
		PAGE_CACHE_MASK	= (~(PAGE_CACHE_SIZE-1));
		info ("PAGE_CACHE_SHIFT=%d, PAGE_CACHE_SIZE=%d, PAGE_CACHE_MASK=0x%x\n",
		      PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE, PAGE_CACHE_MASK);
	}

/*
	trap_signal( SIGBUS );
	trap_signal( SIGSEGV );
*/
	if( getenv( "REISER4_TRACE_FLAGS" ) != NULL ) {
		reiser4_current_trace_flags = 
			strtol( getenv( "REISER4_TRACE_FLAGS" ), NULL, 0 );
		/*rlog( "nikita-1496", "reiser4_current_trace_flags: %x", 
		  get_current_trace_flags() );*/
	}


	/*
	 * FIXME-VS: will be fixed
	 */
	if (argc == 2 && !strcmp (argv[1], "sh")) {
		bash_test (argc, argv, 0);
	}


	root_block = 3ull;
	tree_height = 1;
	next_to_use = 0x10000ull;
	new_block_nr = 10;
	mmap_fname = getenv( "REISER4_UL_DURABLE_MMAP" );
	mmap_meta_fname = getenv( "REISER4_UL_DURABLE_MMAP_META" );
	mmap_meta_fd = -1;
	root_dir_key = ROOT_DIR_KEY;
	fs_is_here = 0;
	if( ( mmap_fname != NULL ) && ( mmap_meta_fname != NULL ) ) {
		mmap_back_end_fd = open( mmap_fname, O_CREAT | O_RDWR, 0700 );
		if( mmap_back_end_fd == -1 ) {
			fprintf( stderr, "%s: Cannot open %s: %s\n", argv[ 0 ],
				 mmap_fname, strerror( errno ) );
			exit( 1 );
		}
		mmap_back_end_size = lseek( mmap_back_end_fd, (off_t)0, SEEK_END );
		if( ( off_t ) mmap_back_end_size == ( off_t ) -1 ) {
			perror( "lseek" );
			exit( 2 );
		}
		mmap_back_end_start = mmap( NULL, 
					    mmap_back_end_size, 
					    PROT_WRITE | PROT_READ, 
					    MAP_SHARED, mmap_back_end_fd, (off_t)0 );
		if( mmap_back_end_start == MAP_FAILED ) {
			perror( "mmap" );
			exit( 3 );
		}
		mmap_meta_fd = open( mmap_meta_fname, O_CREAT | O_RDWR, 0777 );
		if( mmap_meta_fd == -1 ) {
			fprintf( stderr, "%s: Cannot open %s: %s\n", argv[ 0 ],
				 mmap_meta_fname, strerror( errno ) );
		} else {
			char buf[ 100 ];

			if( read( mmap_meta_fd, buf, sizeof buf ) != sizeof buf ) {
				fprintf( stderr, "%s: read error %s\n", 
					 argv[ 0 ], mmap_meta_fname );
				exit( 4 );
			}
			
			if( sscanf( buf, "%lli %i %lli %lli %lli %lli", 
				    &root_block, &tree_height, 
				    &next_to_use, &new_block_nr,
				    &locality, &objectid ) != 6 ) {
				fprintf( stderr, "%s: Wrong conversion in %s\n", 
					 argv[ 0 ], buf );
				exit( 5 );
			} else {
				fs_is_here = 1;
			}
		}
	}

	{
		pthread_t uswapper;

		super.u.generic_sbp = kmalloc (sizeof (reiser4_super_info_data),
					       GFP_KERNEL);
		if( super.u.generic_sbp == NULL )
			BUG();
		xmemset (super.u.generic_sbp, 0, 
			 sizeof (reiser4_super_info_data));

		super.s_op = &reiser4_super_operations;
		super.s_root = &root_dentry;
		super.s_blocksize = PAGE_CACHE_SIZE; /*getenv( "REISER4_BLOCK_SIZE" ) ? 
			atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512; */
		super.s_blocksize_bits = PAGE_CACHE_SHIFT;
		xmemset( &root_dentry, 0, sizeof root_dentry );

		init_context( &__context, &super );

#if REISER4_DEBUG
		atomic_set( &get_current_super_private() -> total_threads, 0 );
		atomic_set( &get_current_super_private() -> active_threads, 0 );
#endif
		get_current_super_private() -> blocks_free = ~0ul;

		assert ("jmacd-998", super.s_blocksize == (unsigned)PAGE_CACHE_SIZE /* don't blame me, otherwise. */);

		register_thread();
		spin_lock_init( &mp_guard );
		kcond_init( &memory_pressed );
		result = pthread_create( &uswapper, NULL, uswapd, &super );
		assert( "nikita-1938", result == 0 );

		/* check that blocksize is a power of two */
		assert( "vs-417", 
			! ( super.s_blocksize & ( super.s_blocksize - 1 ) ) );

		spin_lock_init( &inode_hash_guard );
/*		spin_lock_init( &alloc_guard );*/

		init_inodecache();
		znodes_init();
		init_plugins();
		txn_init_static();
		sys_rand_init();
		txn_mgr_init( &get_super_private (&super) -> tmgr );
		INIT_LIST_HEAD( &inode_hash_list );
		INIT_LIST_HEAD( &page_list );
		init_formatted_fake( &super );
		
		root_dentry.d_inode = NULL;
		/* initialize reiser4_super_info_data's oid plugin */
		get_super_private( &super ) -> oid_plug = &oid_plugins[OID_40_ALLOCATOR_ID].oid_allocator;
		get_super_private( &super ) -> oid_plug ->
			init_oid_allocator( get_oid_allocator( &super ), 1ull, next_to_use );
		/* initialize space plugin */
		{
			reiser4_super_info_data * private;
			reiser4_space_allocator * allocator;

			private = get_super_private( &super );
			private -> space_plug =
				space_allocator_plugin_by_id( TEST_SPACE_ALLOCATOR_ID );
			allocator = get_space_allocator( &super );
			private -> space_plug -> init_allocator( allocator,
								 &super, &next_to_use );
		}

		get_super_private( &super ) -> lplug = layout_plugin_by_id( LAYOUT_40_ID );
		s = &super;
		
		tree = &get_super_private( s ) -> tree;
		result = init_tree( tree, s, &root_block,
				    tree_height, node_plugin_by_id( NODE40_ID ),
				    &page_cache_tops );
		if( result )
			rpanic ("jmacd-500", "znode_tree_init failed");
	}

	if( argc >= 2 ) {
		int i;

		for( i = 0 ; team[ i ].name != NULL ; ++ i ) {
			if( !strcmp( team[ i ].name, argv[ 1 ] ) ) {
				result = team[ i ].func( argc, argv, tree );
				break;
			}
		}
		if( team[ i ].name == NULL )
			fprintf( stderr, "%s: Unknown user %s\n", 
				 argv[ 0 ], argv[ 1 ] );
	} else {
		fprintf( stderr, "Usage: %s user-name\n", argv[ 0 ] );
	}
	if( getenv( "REISER4_PRINT_STATS" ) != NULL )
		reiser4_print_stats();

	if( mmap_meta_fd != -1 ) {
		char buf[ 100 ];
		get_super_private( &super ) -> oid_plug ->
			allocate_oid( get_oid_allocator( &super ), &next_to_use );
		root_block = tree -> root_block;
		tree_height = tree -> height;
		
		lseek( mmap_meta_fd, 0, SEEK_SET );
		sprintf( buf, "%lli %i %lli %lli %lli %lli\n", 
			 root_block, tree_height, next_to_use, ++ new_block_nr,
			 reiser4_inode_data (s->s_root->d_inode)->locality_id,
			 (long long int)s->s_root->d_inode->i_ino);
		write( mmap_meta_fd, buf, strlen( buf ) + 1 );
	}

	info( "tree height: %i\n", tree -> height );

	deregister_thread();

	fresult = txn_mgr_force_commit (s);

	eresult = __REISER4_EXIT( &__context );

	return result ? : (fresult ? : (eresult ? : 0));
}

int main (int argc, char **argv)
{
	int ret;
	
	if ((ret = pthread_key_create (& __current_key, free_current)) != 0) {
		/* okay, but rpanic seg faults if current == NULL :( */
		rpanic ("jmacd-901", "pthread_key_create failed");
	}

	/* Some init functions need to run before REISER4_ENTRY */
	init_context_mgr();

	ret = real_main (argc, argv);

	txn_done_static ();

	return ret;
}	

void funJustAfterMain()
{}

#if REISER4_DEBUG

/** helper called by print_tree_rec() */
void tree_rec_dot( reiser4_tree *tree /* tree to print */, 
		   znode *node /* node to print */, 
		   __u32 flags /* print flags */, 
		   FILE *dot /* dot-output */ )
{
	int ret;
	coord_t coord;
	char buffer_l[ 100 ];
	char buffer_r[ 100 ];

	ret = zload( node );
	if( ret != 0 ) {
		info( "Cannot load/parse node: %i", ret );
		return;
	}

	fprintf( dot, "B%lli [shape=record,label=\"%lli\\n%s\\n%s\"];\n", 
		 *znode_get_block( node ), 
		 *znode_get_block( node ),
		 sprintf_key( buffer_l, &node -> ld_key ),
		 sprintf_key( buffer_r, &node -> rd_key ) );

	for( coord_init_before_first_item( &coord, node ); coord_next_item( &coord ) == 0; ) {

		if( item_is_internal( &coord ) ) {
			znode *child;

			spin_lock_dk( current_tree );
			child = child_znode( &coord, 0 );
			spin_unlock_dk( current_tree );
			if( !IS_ERR( child ) ) {
				tree_rec_dot( tree, child, flags, dot );
				fprintf( dot, "B%lli -> B%lli ;\n", 
					 *znode_get_block( node ),
					 *znode_get_block( child ) );
				zput( child );
			} else {
				info( "Cannot get child: %li\n", 
				      PTR_ERR( child ) );
			}
		}
	}
	zrelse( node );
	/*
	if( flags & REISER4_NODE_PRINT_HEADER && znode_get_level( node ) != LEAF_LEVEL )
		print_address( "end children of node", znode_get_block( node ) );
	*/
}

#endif

void __mark_inode_dirty(struct inode *inode UNUSED_ARG, int flags UNUSED_ARG)
{
}

/*
 * For address_spaces which do not use buffers.  Just set the page's dirty bit
 * and move it to the dirty_pages list.  Also perform space reservation if
 * required.
 *
 * __set_page_dirty_nobuffers() may return -ENOSPC.  But if it does, the page
 * is still safe, as long as it actually manages to find some blocks at
 * writeback time.
 *
 * This is also used when a single buffer is being dirtied: we want to set the
 * page dirty in that case, but not all the buffers.  This is a "bottom-up"
 * dirtying, whereas __set_page_dirty_buffers() is a "top-down" dirtying.
 */
int __set_page_dirty_nobuffers(struct page *page)
{
	int ret = 0;

	if (!TestSetPageDirty(page)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			spin_lock( &page_list_guard );
			list_del(&page->mapping_list);
			list_add(&page->mapping_list, &mapping->dirty_pages);
			spin_unlock( &page_list_guard );
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
		}
	}
	return ret;
}

/**
 * write_one_page - write out a single page and optionally wait on I/O
 *
 * @page - the page to write
 * @wait - if true, wait on writeout
 *
 * The page must be locked by the caller and will be unlocked upon return.
 *
 * write_one_page() returns a negative error code if I/O failed.
 */
int write_one_page(struct page *page, int wait)
{
	struct address_space *mapping = page->mapping;
	int ret = 0;

	assert ("nikita-2104", PageLocked(page));

	if (wait && PageWriteback(page))
		wait_on_page_writeback(page);

	spin_lock(&mapping->page_lock);
	list_del(&page->list);
	if (TestClearPageDirty(page)) {
		list_add(&page->list, &mapping->locked_pages);
		page_cache_get(page);
		spin_unlock(&mapping->page_lock);
		ret = mapping->a_ops->writepage(page);
		if (ret == 0 && wait) {
			wait_on_page_writeback(page);
			if (PageError(page))
				ret = -EIO;
		}
		page_cache_release(page);
	} else {
		list_add(&page->list, &mapping->clean_pages);
		spin_unlock(&mapping->page_lock);
		unlock_page(page);
	}
	return ret;
}

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

