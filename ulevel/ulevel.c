/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation.
 */
#define _GNU_SOURCE

#include "../reiser4.h"


int fs_is_here;

static void
SUSPEND_CONTEXT( reiser4_context *context )
{
	if( __REISER4_EXIT( context ) != 0) {                
		rpanic( "jmacd-533", "txn_end failed");   
	}
}

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

void *xmalloc( size_t size )
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

	addr = xmalloc( size );

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

void *kmem_cache_alloc( kmem_cache_t *slab, int gfp_flag UNUSE )
{
	void *addr;

	addr = kmalloc( slab -> size, 0 );
	xmemset( addr, 0, slab -> size );

	if (addr) {
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

struct super_block * get_sb_bdev (struct file_system_type *fs_type UNUSED_ARG,
				  int flags, char *dev_name UNUSED_ARG, 
				  void * data,
				  int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block * s;
	int result;

	s = &super_blocks[0];
	s->s_flags = flags;
	s->s_blocksize = 1024;
	/* not yet */
	if (dev_name)
		BUG ();

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



static struct super_block * do_mount (char * dev_name)
{
	struct file_system_type * fs;

	fs = find_filesystem ("reiser4");
	if (!fs)
		return 0;

	return fs->get_sb (fs, 0/*flags*/, dev_name, 0/*data*/);
}



/****************************************************************************/


static spinlock_t inode_hash_guard;
struct list_head inode_hash_list;

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
	spin_lock( &inode_hash_guard );
	list_add (&inode->i_hash, &inode_hash_list);
	spin_unlock( &inode_hash_guard );
	return inode;
}

struct inode * new_inode (struct super_block * sb)
{
	struct inode * inode;

	inode = alloc_inode (sb);
	inode->i_nlink = 1;
	return inode;
}


int init_special_inode( struct inode *inode UNUSED_ARG, __u32 mode UNUSED_ARG,
			__u32 rdev UNUSED_ARG )
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


static struct inode * find_inode (struct super_block *super UNUSED_ARG,
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
		if (!test(inode, data))
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
			if (set(inode, data))
				goto set_failed;

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

static struct page * new_page (struct address_space * mapping,
			       unsigned long ind)
{
	struct page * page;

	page = kmalloc (sizeof (struct page), 0);
	assert ("vs-288", page);
	xmemset (page, 0, sizeof (struct page));

	page->index = ind;
	page->mapping = mapping;
	page->count = 1;

	page->virtual = kmalloc (PAGE_SIZE, 0);
	assert ("vs", page->virtual);
	xmemset (page->virtual, 0, PAGE_SIZE);

	list_add (&page->list, &page_list);
	return page;
}


void lock_page (struct page * p)
{
	assert ("vs-287", !PageLocked (p));\
	(p)->flags |= PG_locked;\
}


void unlock_page (struct page * p)
{
	assert ("vs-286", PageLocked (p));\
	(p)->flags &= ~PG_locked;\
}


void remove_inode_page (struct page * page)
{
	assert ("vs-618", (page->count == 1 &&
			   PageLocked (page)));
	page->mapping = 0;
}


struct page * find_get_page (struct address_space * mapping,
			     unsigned long ind)
{
	struct list_head * cur;
	struct page * page;


	list_for_each (cur, &page_list) {
		page = list_entry (cur, struct page, list);
		if (page->index == ind && page->mapping == mapping) {
			page->count ++;
			return page;
		}
	}
	return 0;
}


static void truncate_inode_pages (struct address_space * mapping,
				  loff_t from)
{
	struct list_head * cur;
	struct page * page;
	unsigned ind;


	ind = (from + PAGE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	list_for_each (cur, &page_list) {
		page = list_entry (cur, struct page, list);
		if (page->mapping == mapping) {
			if (page->index >= ind) {
				page->count ++;
				lock_page (page);
				remove_inode_page (page);
				unlock_page (page);
				page->count --;
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
	
	if (!Page_Uptodate (page)) {
		lock_page (page);
		filler (data, page);
	}
	return page;
}


void wait_on_page(struct page * page)
{
	if (PageLocked (page)) {
		struct buffer_head * bh;
		int notuptodate = 0;

		/* wait until all buffers are unlocked */
		bh = page->buffers;
		do {
			wait_on_buffer (bh);
			if (!buffer_uptodate (bh))
				notuptodate ++;
		} while (bh = bh->b_this_page, bh != page->buffers);

		reiser4_stat_file_add (wait_on_page);
		unlock_page (page);
		if (!notuptodate)
			SetPageUptodate (page);
	}
}


static void print_page (struct page * page)
{
	struct buffer_head * bh;

	if (!page->mapping)
		return;
	info ("PAGE: index %lu, count %d, ino %lx%s%s\nbuffers:\n",
	      page->index, page->count, page->mapping->host->i_ino,
	      PageLocked (page) ? ", Locked" : "",
	      Page_Uptodate (page) ? ", Uptodate" : "");


	bh = page->buffers;
	if (!bh) {
		info ("NULL\n;");
		return;
	}
	do {
		info ("%Lu%s%s%s%s%s%s\n",
		      bh->b_blocknr,
		      buffer_mapped (bh) ? ", Mapped" : "",
		      buffer_uptodate (bh) ? ", Uptodate" : "",
		      buffer_locked (bh) ? ", Locked" : "",
		      buffer_new (bh) ? ", New" : "",
		      buffer_unallocated (bh) ? ", Unallocated" : "",
		      buffer_allocated (bh) ? ", Allocated" : "");
	} while (bh = bh->b_this_page, bh != page->buffers);
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
	return page->virtual;
}


void kunmap (struct page * page UNUSED_ARG)
{
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

/* mm/page_alloc.c */
void page_cache_release (struct page * page)
{
	assert ("vs-352", page->count > 0);
	page->count --;
}


/* fs/buffer.c */
int create_empty_buffers (struct page * page, unsigned blocksize)
{
	int i;
	struct buffer_head * bh, * last;


	assert ("vs-292", !page->buffers);

	bh = NULL;
	last = NULL;
	for (i = PAGE_SIZE / blocksize - 1; i >= 0; i --) {
		bh = kmalloc (sizeof (struct buffer_head), 1);
		assert ("vs-250", bh);
		if (page->buffers)
			bh->b_this_page = page->buffers;
		else
			last = bh;
		page->buffers = bh;
		bh->b_data = (char *)page->virtual + i * blocksize;
		bh->b_size = blocksize;
		bh->b_blocknr = 0;
		bh->b_state = 0;
	}
	last->b_this_page = bh;
	return 0;
}


void map_bh (struct buffer_head * bh, struct super_block * sb, reiser4_block_nr block)
{
	mark_buffer_mapped (bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_dev = sb->s_dev;
	bh->b_blocknr = block;
}

void ll_rw_block (int rw UNUSED_ARG, int nr UNUSED_ARG,
		  struct buffer_head ** pbh UNUSED_ARG)
{
}

void set_buffer_async_io (struct buffer_head * bh UNUSED_ARG)
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
		make_buffer_uptodate (bh, 1);
	}
}



#define STYPE( type ) info( #type "\t%i\n", sizeof( type ) )

char *__prog_name;

static int mmap_back_end_fd = -1;
static char *mmap_back_end_start = NULL;
static size_t mmap_back_end_size = 0;

int ulevel_read_node( const reiser4_block_nr *addr, char **data, size_t blksz )
{
	if( mmap_back_end_fd > 0 ) {
		off_t start;

		start = *addr * blksz;
		if( start + blksz > mmap_back_end_size ) {
			warning( "nikita-1372", "Trying to access beyond the device: %li > %u",
				 start, mmap_back_end_size );
			return -EIO;
		} else {
			++ total_allocations;
			
			if (total_allocations > MEMORY_PRESSURE_THRESHOLD)
				declare_memory_pressure();

			*data = mmap_back_end_start + start;
			return blksz;
		}
	} else {
		*data = xmalloc( blksz );
		if( *data != NULL )
			return blksz;
		else
			return -ENOMEM;
	}
}

int ulevel_allocate_node( znode *node )
{
	size_t blksz;

	blksz = reiser4_get_current_sb ()->s_blocksize;

	if( mmap_back_end_fd > 0 ) {
		int ret;

		ret = ulevel_read_node( znode_get_block( node ), &node -> data,
					reiser4_get_current_sb() -> s_blocksize );
		if( ret > 0 ) {
			node -> size = ret;
			return 0;
		} else
			return ret;
	}
	assert( "nikita-1909", node != NULL );
	node -> size = blksz;
	node -> data = xmalloc( node -> size );

	if( node -> data != NULL )
		return 0;
	else
		return -ENOMEM;
}


int reiser4_sb_bread (struct super_block * sb, struct buffer_head * bh)
{
	int result;
	reiser4_tree * tree;

	assert ("vs-634", bh->b_count == 0);
	bh->b_count = 1;

	if (sb && get_super_private (sb) && get_super_private (sb)->tree.read_node) {
		tree = &get_super_private (sb)->tree;
		result = tree->read_node (&bh->b_blocknr, &bh->b_data, bh->b_size);
	} else
		result =  ulevel_read_node (&bh->b_blocknr, &bh->b_data, bh->b_size);
	return result < 0 ? result : 0;


}


void reiser4_sb_bwrite (struct buffer_head * bh UNUSED_ARG)
{
	if (mmap_back_end_fd > 0) {
		;
	} else
		impossible ("vs-635", "no write");
}


void reiser4_sb_brelse (struct buffer_head * bh)
{
	assert ("vs-472", bh->b_count > 0);
	bh->b_count --;
	if (mmap_back_end_fd > 0) {
		;
	} else
		free (bh->b_data);
}


int sb_set_blocksize(struct super_block *sb, int size)
{
	sb->s_blocksize = size;
	return size;
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
		       unsigned int level, const reiser4_block_nr *addr, int init_node_p )
{
	znode *root;
	int    result;

	root = zget( tree, addr, parent, level, GFP_KERNEL );

	if( znode_above_root( root ) ) {
		ZF_SET( root, ZNODE_LOADED );
		add_d_ref( root );
		root -> ld_key = *min_key();
		root -> rd_key = *max_key();
		root -> data = xmalloc( 1 );
		return root;
	}
	if( ( mmap_back_end_fd == -1 ) || init_node_p ) {
		root -> nplug = node_plugin_by_id( NODE40_ID );
		result = zinit_new( root );
		if( result == 0 )
			zrelse( root );
	} else {
		result = zload( root );
	}
	assert( "nikita-1171", result == 0 );
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
		key[ i ] = xmalloc( sizeof key[ i ][ 0 ] * size );
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
		reiser4_context *ctx;

		info.eof = 1;
		ctx = get_current_context();
		SUSPEND_CONTEXT( ctx );
		result = dir -> f_dentry -> d_inode -> i_fop -> 
			readdir( dir, &info, 
				 flags ? one_shot_filldir : echo_filldir );
		init_context( ctx, dir -> f_dentry -> d_inode -> i_sb );
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
	reiser4_context *old_context;

	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	if (sb->s_op->put_super)
		sb->s_op->put_super (sb);

	init_context( old_context, sb );
}


static int call_unlink( struct inode * dir, struct inode *victim, 
			const char *name, int dir_p )
{
	reiser4_context *old_context;
	struct dentry guillotine;
	int result;

	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	xmemset( &guillotine, 0, sizeof guillotine );
	guillotine.d_inode = victim;
	guillotine.d_name.name = name;
	guillotine.d_name.len = strlen( name );
	if( dir_p )
		result = dir -> i_op -> rmdir( dir, &guillotine );
	else
		result = dir -> i_op -> unlink( dir, &guillotine );
	init_context( old_context, dir -> i_sb );
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
		reiser4_context *old_context;
		int r;

		old_context = get_current_context();
		SUSPEND_CONTEXT( old_context );
		r = dir -> i_op -> link( &old_dentry, dir, &new_dentry );
		init_context( old_context, dir -> i_sb );
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
	reiser4_context   *old_context;
	int                ret;
	struct file        df;

	old_context = get_current_context();

	sprintf( dir_name, "Dir-%i", current_pid );
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = dir_name;
	dentry.d_name.len = strlen( dir_name );
	SUSPEND_CONTEXT( old_context );
	ret = info -> dir -> i_op -> mkdir( info -> dir, 
					    &dentry, S_IFDIR | 0777 );
	rlog( "nikita-1638", "In directory: %s", dir_name );
	init_context( old_context, info -> dir -> i_sb );

	if( ret != 0 ) {
		rpanic( "nikita-1636", "Cannot create dir: %i", ret );
	}
	
	f = dentry.d_inode;
	for( i = 0 ; i < info -> num ; ++ i ) {
		__u64 fno;
		struct timespec delay;
		const char *op;
		
		fno = lc_rand_max( ( __u64 ) info -> max );
		
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

	el = xmalloc( sizeof *el );
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
	tree_coord coord;
	carry_insert_data cdata;
	int i;

	assert( "nikita-1096", tree != NULL );

	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block, 
			       !strcmp( argv[ 2 ], "mkfs" ) );
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
		tid = xmalloc( threads * sizeof tid[ 0 ] );
		assert( "nikita-1495", tid != NULL );

		print_inode( "inode", f );

		ret = call_create( f, "foo" );
		info( "ret: %i\n", ret );
		ret = call_create( f, "bar" );
		info( "ret: %i\n", ret );
		spin_lock_init( &lc_rand_guard );
		memset( &info, 0, sizeof info );
		info.dir = f;
		if (argc == 5)
			info.num = atoi( argv[ 4 ] );
		else
			info.num = 1;
		info.max = info.num;
		info.sleep = 0;
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

		tid = xmalloc( ( threads + 2 ) * sizeof tid[ 0 ] );
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

			init_coord( &coord );
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
			done_coord( &coord );

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
			coord_first_unit( &coord, NULL );

			set_key_locality( &key, 2ull + i );

			coord.between = ( i == 0 ) ? AT_UNIT : AFTER_UNIT;
			info( "_____________%i_____________\n", i );
			print_coord( "before", &coord, 1 );
			ret = carry( &lowest_level, NULL );
			printf( "result: %i\n", ret );
			done_carry_pool( &pool );
			print_coord( "after", &coord, 1 );
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

			init_coord( &coord );
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
			done_coord( &coord );
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
		STYPE( tree_coord );
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
		STYPE( tree_coord );
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
	tree_coord coord;
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
		coord_first_unit( &coord, NULL );
	
		coord.between = AT_UNIT;
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
	tree_coord coord;
	lock_handle lh;
	tree_level level;
	int result;
	inter_syscall_rap ra;

	init_coord (&coord);
	init_lh (&lh);

	level = (item_id_by_plugin (data->iplug) == EXTENT_POINTER_ID) ? TWIG_LEVEL : LEAF_LEVEL;
	result = insert_by_key (tree, key, data, &coord, &lh,
				level, &ra, 0, 
				CBK_UNIQUE);

	done_lh (&lh);
	done_coord (&coord);
	return result;
}

static int call_create (struct inode * dir, const char * name)
{
	reiser4_context *old_context;
	struct dentry dentry;
	int ret;


	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen( name );
	ret = dir->i_op -> create( dir, &dentry, S_IFREG | 0777 );

	init_context( old_context, dir->i_sb );
	if( ret == 0 )
		iput( dentry.d_inode );
	return ret;
}


static ssize_t call_write (struct inode * inode, const char * buf,
			   loff_t offset, unsigned count)
{
	reiser4_context *old_context;
	ssize_t result;
	struct file file;
	struct dentry dentry;


	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	file.f_dentry = &dentry;
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = inode;
	result = inode->i_fop->write (&file, buf, count, &offset);

	init_context (old_context, inode->i_sb);

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
	reiser4_context *old_context;
	ssize_t result;
	struct file file;
	struct dentry dentry;


	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	file.f_dentry = &dentry;
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = inode;
	result = inode->i_fop->read (&file, buf, count, &offset);

	init_context (old_context, inode->i_sb);
	return result;
}


void call_truncate (struct inode * inode, loff_t size)
{
	reiser4_context *old_context;

	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );


	truncate_inode_pages (inode->i_mapping, size);
	inode->i_size = size;

	inode->i_op->truncate (inode);
	init_context (old_context, inode->i_sb);
}


static struct inode * call_lookup (struct inode * dir, const char * name)
{
	struct dentry dentry;
	struct dentry * result;
	reiser4_context *old_context;

	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->lookup (dir, &dentry);
	init_context (old_context, dir->i_sb);

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
	reiser4_context *old_context;
	struct dentry dentry;
	int result;


	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->mkdir (dir, &dentry, S_IFDIR | 0777);

	init_context (old_context, dir->i_sb);
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


int alloc_extent (reiser4_tree *, tree_coord *,
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

	buf = xmalloc (BUFSIZE);
	if (!buf) {
		perror ("copy_file: xmalloc failed");
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


static int get_depth (const char * path)
{
	int i;
	const char * slash;

	i = 1;
	for (slash = path; (slash = strchr (slash, '/')) != 0; i ++, slash ++);
	return i;
}


static const char * last_name (const char * full_name)
{
	const char * name;
	
	name = strrchr (full_name, '/');
	return name ? (name + 1) : full_name;
}


#if 0

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


#include <dirent.h>

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


static void open_device (const char * fname)
{
	mmap_back_end_fd = open( fname, O_RDWR, 0700 );
	if( mmap_back_end_fd == -1 ) {
		perror( "open" );
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
}


static void close_device (void)
{
	if (munmap (mmap_back_end_start, mmap_back_end_size)) {
		perror( "munmap" );
		exit( 3 );
	}
	mmap_back_end_start = 0;	
}



static int bash_mount (reiser4_context * context, const char * file_name)
{
	struct super_block * sb;

	open_device (file_name);

	sb = do_mount (0);
	if (IS_ERR (sb)) {
		close_device ();
		return PTR_ERR (sb);
	}

	/* REISER4_ENTRY */
	init_context (context, sb);
	return 0;
}


static void bash_umount (reiser4_context * context)
{
	int ret;

	call_umount (reiser4_get_current_sb ());
	invalidate_inodes ();

	/* REISER4_EXIT */
        ret = txn_end (context);
	done_context (context);

	/*
	txn_mgr_force_commit (s);
	*/
	close_device ();
}


/* this creates reiser4 filesystem of TEST_LAYOUT_ID */
static int bash_mkfs (const char * file_name)
{
	znode * fake, * root;
	struct super_block super;
	struct dentry root_dentry;
	reiser4_block_nr root_block;
	oid_t next_oid;
	reiser4_block_nr next_block;
	reiser4_tree * tree;
	int result;

	
	open_device (file_name);

	super.u.generic_sbp = kmalloc (sizeof (reiser4_super_info_data),
				       GFP_KERNEL);
	if( super.u.generic_sbp == NULL )
		BUG();
	xmemset (super.u.generic_sbp, 0, 
		 sizeof (reiser4_super_info_data));
	super.s_op = &reiser4_super_operations;
	super.s_root = &root_dentry;
	super.s_blocksize = getenv( "REISER4_BLOCK_SIZE" ) ? 
		atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
	xmemset( &root_dentry, 0, sizeof root_dentry );

	{

		REISER4_ENTRY( &super );
		txn_mgr_init( &get_super_private (&super) -> tmgr );


		get_super_private (&super)->lplug = layout_plugin_by_id (TEST_LAYOUT_ID);

		next_oid = 1000ull;
		get_super_private (&super)->oid_plug = oid_allocator_plugin_by_id (OID_40_ALLOCATOR_ID);
		get_super_private (&super)->oid_plug->
			init_oid_allocator (get_oid_allocator (&super), 1ull, next_oid);

		root_block = 3ull;
		next_block = root_block + 1;
		get_super_private (&super)->space_plug = space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID);
		get_super_private (&super)->space_plug->
			init_allocator (get_space_allocator( &super ),
					&super, &next_block );

		/*  make super block */
		{
			struct buffer_head bh;
			reiser4_master_sb * master_sb;
			test_disk_super_block * test_sb;
			size_t blocksize;

			blocksize = super.s_blocksize;
			bh.b_blocknr = REISER4_MAGIC_OFFSET / blocksize;
			bh.b_data = 0;
			bh.b_count = 0;
			bh.b_size = blocksize;
			reiser4_sb_bread (0, &bh);
			memset (bh.b_data, 0, blocksize);

			master_sb = (reiser4_master_sb *)bh.b_data;
			strncpy (master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4);
			cputod16 (TEST_LAYOUT_ID, &master_sb->disk_plugin_id);
			cputod16 (blocksize, &master_sb->blocksize);

			test_sb = (test_disk_super_block *)(bh.b_data + sizeof (*master_sb));
			strncpy (test_sb->magic, TEST_MAGIC, strlen (TEST_MAGIC));
			/* root block and tree height will be changed on umount */
			cputod64 (root_block, &test_sb->root_block);
			cputod16 (1, &test_sb->tree_height);
			cputod16 (HASHED_DIR_PLUGIN_ID, &test_sb->root_dir_plugin);
			cputod16 (DEGENERATE_HASH_ID, &test_sb->root_hash_plugin);
			cputod16 (NODE40_ID, &test_sb->node_plugin);

			{
				oid_t oid;
			
				get_super_private (&super)->oid_plug->allocate_oid (get_oid_allocator (&super),
										    &oid);
				cputod64 (oid, &test_sb->next_to_use);
			}

			assert ("vs-640",
				get_super_private (&super)->space_plug ==
				space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID));
			cputod64 (get_space_allocator (&super)->u.test.new_block_nr,
				  &test_sb->new_block_nr);

			reiser4_sb_bwrite (&bh);
			reiser4_sb_brelse (&bh);
		}

		/* initialize empty tree */
		tree = &get_super_private( &super ) -> tree;
		result = init_tree( tree, &root_block,
				    1/*tree_height*/, node_plugin_by_id( NODE40_ID ),
				    ulevel_read_node, ulevel_allocate_node );
		fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 );
		root = allocate_znode( tree, fake, tree->height, &tree->root_block, 1);
		root -> rd_key = *max_key();
		sibling_list_insert( root, NULL );

		zput (root);
		zput (fake);

		{
			int result;
			struct inode * fake_parent, * inode;

			reiser4_stat_data_base sd;
			reiser4_item_data insert_data;
			reiser4_key key;

			/* key */
			key_init( &key );
			set_key_type( &key, KEY_SD_MINOR );
			set_key_locality( &key, 1ull );
			set_key_objectid( &key, 2ull );

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
			call_create (fake_parent, ".");
			call_create (fake_parent, "x");
			inode = call_lookup (fake_parent, "x");
			if (!inode)
				return 1;
			call_write2 (inode, (loff_t)0, super.s_blocksize);
			call_unlink (fake_parent, inode, "x", 0);
			inode->i_state &= ~I_DIRTY;
			iput (inode);

			call_mkdir (fake_parent, "root");

			/* inode of root directory */
			inode = call_lookup (fake_parent, "root");

			{
				/* cut everything but root directory */
				reiser4_key from;
				reiser4_key to;

				build_sd_key (fake_parent, &from);
				build_sd_key (inode, &to);

				set_key_objectid (&to, get_key_objectid (&to) - 1);
				set_key_offset (&to, get_key_offset (max_key ()));

				result = cut_tree (tree, &from, &to);
				if (result)
					return result;
			}
			fake_parent->i_state &= ~I_DIRTY;
			inode->i_state &= ~I_DIRTY;
			iput (fake_parent);
			super.s_root->d_inode = inode;
			call_umount (&super);
			invalidate_inodes ();
		}

		/*print_tree_rec ("mkfs", tree, REISER4_NODE_PRINT_ALL);*/

		result = __REISER4_EXIT( &__context );
	}
	close_device ();
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
	
	buf1 = xmalloc (BUFSIZE);
	buf2 = xmalloc (BUFSIZE);
	if (!buf1 || !buf2) {
		perror ("diff: xmalloc failed");
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



#include <string.h>

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
	
	buf = xmalloc (count);
	if (!buf) {
		info ("read: xmalloc failed\n");
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
	reiser4_context *old_context;

	old_context = get_current_context();
	SUSPEND_CONTEXT( old_context );

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

	init_context( old_context, cwd -> i_sb );
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
	tree_coord coord;
	lock_handle lh;
	reiser4_key key;
	int result;


	init_coord (&coord);
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
	coord_first_unit (&coord, NULL);
	result = iterate_tree (tree, &coord, &lh, 
				       alloc_extent, 0, ZNODE_WRITE_LOCK, 0);

	done_lh (&lh);
	done_coord (&coord);

	print_tree_rec ("AFTER ALLOCATION", tree, REISER4_NODE_PRINT_HEADER |
			REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
}


/*
 * tree_iterate actor
 */
int squalloc_right_neighbor (znode * left, znode * right, 
			     reiser4_blocknr_hint *preceder);
static int do_twig_squeeze (reiser4_tree * tree, tree_coord * coord,
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
	coord_last_unit (coord, 0);
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
	coord_last_unit (coord, 0);
	return 1;
}

/*
 * go through all "twig" nodes and call squeeze_right_neighbor
 */
static void squeeze_twig_level (reiser4_tree * tree)
{
	tree_coord coord;
	lock_handle lh;
	reiser4_key key;
	int result;


	init_coord (&coord);
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
	coord_first_unit (&coord, NULL);
	result = iterate_tree (tree, &coord, &lh, 
				       do_twig_squeeze, 0, ZNODE_WRITE_LOCK, 0/* through items */);

	done_lh (&lh);
	done_coord (&coord);

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
		tree_coord            coord;
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

		init_coord (& coord);
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
		tree_coord             coord;
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

		init_coord (& coord);
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

	proc_ids              = xmalloc (sizeof (pthread_t) * procs);
	_jmacd_exists_map     = xmalloc (_jmacd_items);

	for (i = 0; i < _jmacd_items; i += 1) {
		_jmacd_exists_map[i] = 1;
	}

	/* These four magic lines are taken from nikita_test, and seem to be
	 * necessary--maybe they belong somewhere else... */
	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block, 
			       0 );
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

#define BLOCK_COUNT 10000

/* tree op. read node which emulates read from valid reiser4 volume  */
static int bm_test_read_node (const reiser4_block_nr *addr, char **data, size_t blksz )
{
	struct super_block * super = get_current_context() -> super;
	int bmap_nr;
	reiser4_block_nr bmap_block_addr;

	assert ("zam-413", *data == NULL);

	if (blocknr_is_fake(addr)) {
		*data = reiser4_kmalloc (blksz, GFP_KERNEL);

		if (*data == NULL) return -ENOMEM;

		xmemset (*data, 0, blksz);

		return 0;
	}

	/* it is a hack for finding what block we read (bitmap block or not) */
	bmap_nr = *addr / (super->s_blocksize * 8);
	get_bitmap_blocknr (super,  bmap_nr, &bmap_block_addr);

	if (disk_addr_eq (addr, &bmap_block_addr)) {
		int offset = *addr - bmap_nr * blksz * 8;
		
		*data = reiser4_kmalloc (blksz, GFP_KERNEL);

		if (*data == NULL) return -ENOMEM;

		xmemset(*data, 0, blksz);
		set_bit(offset, (long*)*data);

	} else {
		warning ("zam-411", "bitmap test should not read not bitmap block #%llu", *addr);
		return -EIO;
	}

	return 0;
}

/** a temporary solutions for setting up reiser4 super block */
static void fill_sb (struct super_block * super)
{
	reiser4_super_info_data * info_data = get_super_private (super);
	
	info_data -> block_count = BLOCK_COUNT;
	info_data -> blocks_used  = BLOCK_COUNT / 10;
	info_data -> blocks_free  = info_data -> block_count - info_data -> blocks_free;

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

	tree -> read_node = bm_test_read_node;


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

			ret = reiser4_alloc_blocks (&hint, &block, &len);

			blocknr_hint_done (&hint);

			if (ret != 0) break;

			++ count;
			total += (int)len;

			printf ("allocated %d blocks in %d attempt(s)\n", total, count);

		}

		printf ("total %d blocks allocated until %d error (%s) returned\n", total, ret, strerror(ret));
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

		SUSPEND_CONTEXT( &__context );
		result = memory_pressure( super );
		init_context( &__context, super );

		if( result != 0 )
			warning( "nikita-1937", "flushing failed: %i", result );
	}
	return NULL;
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
	trap_signal( SIGBUS );
	trap_signal( SIGSEGV );
*/
	if( getenv( "REISER4_TRACE_FLAGS" ) != NULL ) {
		reiser4_current_trace_flags = 
			strtol( getenv( "REISER4_TRACE_FLAGS" ), NULL, 0 );
		rlog( "nikita-1496", "reiser4_current_trace_flags: %x", 
		      get_current_trace_flags() );
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
		super.s_blocksize = getenv( "REISER4_BLOCK_SIZE" ) ? 
			atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
		xmemset( &root_dentry, 0, sizeof root_dentry );

		init_context( &__context, &super );

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
		INIT_LIST_HEAD( &inode_hash_list );
		INIT_LIST_HEAD( &page_list );
		
		tree = &get_super_private( s ) -> tree;
		result = init_tree( tree, &root_block,
				    tree_height, node_plugin_by_id( NODE40_ID ),
				    ulevel_read_node, ulevel_allocate_node );
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

	eresult = __REISER4_EXIT( &__context );

	fresult = txn_mgr_force_commit (s);

	return result ? : (eresult ? : (fresult ? : 0));
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
