/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation.
 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "../reiser4.h"

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

int sema_init( semaphore *sem, int value )
{
	pthread_mutex_init( &sem -> mutex, NULL );
	if( value == 0 )
		pthread_mutex_lock( &sem -> mutex );
	return 0;
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

inline void *ERR_PTR(long error)
{
	return (void *) error;
}

inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

inline long IS_ERR(const void *ptr)
{
	return (unsigned long)ptr > (unsigned long)-1000L;
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

#if 0
static void kcondvar_checklock (kcondvar_t  *kcond)
{
	assert ("jmacd-59", spin_is_locked (kcond->_lockp));
}

void kcondvar_init (kcondvar_t *kcond,
		    spinlock_t *lock)
{
	kcond->_lockp = lock;
	kcond->_count = 0;

	pthread_cond_init (& kcond->_cond, NULL);
}

int kcondvar_waiters (kcondvar_t *kcond)
{
	kcondvar_checklock (kcond);

	return kcond->_count;
}

void kcondvar_wait (kcondvar_t *kcond)
{
	kcondvar_checklock (kcond);

	kcond->_count += 1;

	pthread_cond_wait (& kcond->_cond, kcond->_lockp);

	kcondvar_checklock (kcond);

	kcond->_count -= 1;
}

void kcondvar_signal (kcondvar_t *kcond)
{
	pthread_cond_signal (& kcond->_cond);
}

void kcondvar_broadcast (kcondvar_t *kcond)
{
	pthread_cond_broadcast (& kcond->_cond);
}
#endif

#define KMEM_CHECK 0
#define KMEM_MAGIC 0x74932123U

void *kmalloc( size_t size, int flag UNUSE )
{
	__u32 *addr;

#if KMEM_CHECK	
	size += sizeof (__u32);
#endif

	addr = malloc( size );

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
	list_add (&inode->i_hash, &inode_hash_list);
	return inode;
}


struct inode * get_new_inode (struct super_block * sb, unsigned long ino)
{
	struct inode * inode;
	
	inode = alloc_inode (sb);
	inode->i_ino = ino;
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
				  unsigned long ino)
{
	struct list_head * cur;
	struct inode * inode;


	list_for_each (cur, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		if (inode->i_ino == ino)
			return inode;
	}
	return 0;
}


struct inode *iget( struct super_block *super, unsigned long ino )
{
	struct inode * inode;

	inode = find_inode (super, ino);
	if (inode) {
		atomic_inc(&inode->i_count);
		return inode;
	}

	return get_new_inode (super, ino);
}


void iput( struct inode *inode UNUSED_ARG )
{
	atomic_dec(&inode->i_count);
}

void mark_inode_dirty (struct inode * inode)
{
	inode->i_state |= I_DIRTY;
}


void d_instantiate(struct dentry *entry, struct inode * inode)
{
	entry -> d_inode = inode;
}

int  reiser4_alloc_block( znode *neighbor UNUSED_ARG,
			  reiser4_disk_addr *blocknr UNUSED_ARG )
{
	static reiser4_disk_addr new_block_nr = ( reiser4_disk_addr ){ .blk = 10 };

	*blocknr = new_block_nr;
	++ new_block_nr.blk;
	return 0;
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
	
		if ((ret = pthread_setspecific (__current_key, malloc (sizeof (struct task_struct)))) != 0) {
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


struct page * find_lock_page (struct address_space * mapping,
			      unsigned long ind)
{
	struct page * page;

	page = find_get_page (mapping, ind);
	if (page)
		LockPage (page);
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
	LockPage (page);
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
		LockPage (page);
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
		UnlockPage (page);
		if (!notuptodate)
			SetPageUptodate (page);
	}
}


static void print_page (struct page * page)
{
	struct buffer_head * bh;

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


void map_bh (struct buffer_head * bh, struct super_block * sb, block_nr block)
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
static off_t mmap_back_end_size = 0;

int ulevel_read_node( reiser4_tree *tree UNUSED_ARG, 
		      const reiser4_disk_addr *addr UNUSED_ARG, char **data )
{
	if( mmap_back_end_fd > 0 ) {
		off_t start;

		start = addr -> blk * reiser4_get_current_sb ()->s_blocksize;
		if( start + reiser4_get_current_sb ()->s_blocksize > mmap_back_end_size ) {
			warning( "nikita-1372", "Trying to access beyond the device: %Li > %Li",
				 start, mmap_back_end_size );
			return -EIO;
		} else {
			*data = mmap_back_end_start + start;
			return reiser4_get_current_sb ()->s_blocksize;
		}
	} else {
		*data = malloc( reiser4_get_current_sb ()->s_blocksize );
		if( *data != NULL )
			return reiser4_get_current_sb ()->s_blocksize;
		else
			return -ENOMEM;
	}
}

znode *allocate_znode( reiser4_tree *tree, znode *parent,
		       unsigned int level, const reiser4_disk_addr *addr, int init_node_p )
{
	znode *root;
	int    result;

	root = zget( tree, addr, parent, level, GFP_KERNEL );

	if( znode_above_root( root ) ) {
		ZF_SET( root, ZNODE_LOADED );
		atomic_inc( &root -> d_count );
		root -> ld_key = *min_key();
		root -> rd_key = *max_key();
		root -> data = malloc( 1 );
		return root;
	}
	result = zload( root );
	assert( "nikita-1171", result == 0 );
	root -> nplug = node_plugin_by_id( NODE40_ID );
	if( ( mmap_back_end_fd == -1 ) || init_node_p ) {
		zinit_new( root );
	}
	root -> nplug = NULL;
	result = zparse( root );
	assert( "nikita-1170", result == 0 );
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
			found = keycmp( key, &array[ left ] ) == EQUAL_TO;
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
		key[ i ] = malloc( sizeof key[ i ][ 0 ] * size );
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

static int echo_filldir(void *eof, const char *name, int namelen, 
			loff_t offset, ino_t inode, unsigned ftype)
{
	* ( int * ) eof = 0;
	if( lc_rand_max( 10ull ) < 2 )
		return -EINVAL;
	info( "filldir[%i]: %s (%i), %Lx, %Lx, %i\n",
	      current_pid, name, namelen, offset, inode, ftype );
	return 0;
}

static int readdir( struct file *dir )
{
	int eof;
	int result;
	do {
		eof = 1;
		result = dir -> f_dentry -> d_inode -> i_fop -> 
			readdir( dir, &eof, echo_filldir );
	} while( !eof && ( result == 0 ) );
	return result;
}

typedef struct mkdir_thread_info {
	int           max;
	int           num;
	struct inode *dir;
	int           mkdir;
	int           sleep;
} mkdir_thread_info;

void *mkdir_thread( void *arg )
{
	int                i;
	char               dir_name[ 30 ];
	char               name[ 30 ];
	mkdir_thread_info *info;
	struct dentry      dentry;
	struct inode      *f;
	reiser4_context   *old_context;
	int                ret;
	struct file        df;
	REISER4_ENTRY_PTR( ( ( mkdir_thread_info * ) arg ) -> dir -> i_sb );

	info = arg;
	old_context = reiser4_get_current_context();

	sprintf( dir_name, "Dir-%i", current_pid );
	dentry.d_name.name = dir_name;
	dentry.d_name.len = strlen( dir_name );
	SUSPEND_CONTEXT( old_context );
	ret = info -> dir -> i_op -> mkdir( info -> dir, 
					    &dentry, S_IFDIR | 0777 );
	rlog( "nikita-1638", "In directory: %s", dir_name );
	reiser4_init_context( old_context, info -> dir -> i_sb );

	if( ret != 0 ) {
		rpanic( "nikita-1636", "Cannot create dir: %i", ret );
	}
	
	f = dentry.d_inode;
	for( i = 0 ; i < info -> num ; ++ i ) {
		__u64 fno;
		struct timespec delay;

		fno = lc_rand_max( ( __u64 ) info -> max );
		
		sprintf( name, "%i", i );
//		sprintf( name, "%lli-хлоп-Zzzz.", fno );
		dentry.d_name.name = name;
		dentry.d_name.len = strlen( name );
		SUSPEND_CONTEXT( old_context );
		if( info -> mkdir )
			ret = f -> i_op -> mkdir( f, &dentry, S_IFDIR | 0777 );
		else
			ret = f -> i_op -> create( f, &dentry, S_IFREG | 0777 );
		reiser4_init_context( old_context, f -> i_sb );
		info( "(%i) %i: %s/%s: %i\n", current_pid, i, 
		      dir_name, name, ret );
		if( ( ret != 0 ) && ( ret != -EEXIST ) )
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

	dentry.d_inode = f;
	df.f_dentry = &dentry;
	df.f_op = &reiser4_file_operations;
	SUSPEND_CONTEXT( old_context );
	readdir( &df );
	reiser4_init_context( old_context, f -> i_sb );
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
	struct file df;
	struct dentry dd;
	int i;

	assert( "nikita-1096", tree != NULL );

	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block, 
			       !strcmp( argv[ 2 ], "mkfs" ) );
	root -> rd_key = *max_key();
	reiser4_sibling_list_insert( root, NULL );

	if( !strcmp( argv[ 2 ], "mkfs" ) ) {
		/*
		 * root is already allocated/initialised above.
		 */
		info( "Done.\n" );
	} else if( !strcmp( argv[ 2 ], "clean" ) ) {
		ret = cut_tree( tree, min_key(), max_key() );
		printf( "result: %i\n", ret );
	} else if( !strcmp( argv[ 2 ], "print" ) ) {
		print_tree_rec( "tree", tree, (unsigned) atoi( argv[ 3 ] ) );
	} else if( !strcmp( argv[ 2 ], "load" ) ) {
	} else if( !strcmp( argv[ 2 ], "dir" ) || 
		   !strcmp( argv[ 2 ], "mongo" ) ) {
		reiser4_context *old_context;
		int threads;
		pthread_t *tid;
		mkdir_thread_info info;

		reiser4_inode_info rf;
		struct inode *f;
		struct dentry dentry;
		reiser4_item_data data;
		struct {
			reiser4_stat_data_base base;
		} sd;

		f = &rf.vfs_inode;
		threads = atoi( argv[ 3 ] );
		assert( "nikita-1494", threads > 0 );
		tid = malloc( threads * sizeof tid[ 0 ] );
		assert( "nikita-1495", tid != NULL );

		reiser4_init_carry_pool( &pool );
		reiser4_init_carry_level( &lowest_level, &pool );
		
		op = reiser4_post_carry( &lowest_level, 
					 COP_INSERT, root, 0 );
		assert( "nikita-1074", !IS_ERR( op ) && ( op != NULL ) );
		// fill in remaining fields in @op, according to
		// carry.h:carry_op
		cdata.data  = &data;
		cdata.key   = &key;
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
		data.length = sizeof sd.base;
		data.iplug = item_plugin_by_id( SD_ITEM_ID );
		coord_first_unit( &coord );

		key_init( &key );
		set_key_type( &key, KEY_SD_MINOR );
		set_key_locality( &key, 2ull );
		set_key_objectid( &key, 42ull );

		coord.between = AT_UNIT;
		ret = carry( &lowest_level, NULL );
		printf( "result: %i\n", ret );
		info( "_____________sd inserted_____________\n" );
		reiser4_done_carry_pool( &pool );

		xmemset( &rf, 0, sizeof rf );
		INIT_LIST_HEAD( &f -> i_hash );
		INIT_LIST_HEAD( &f -> i_list );
		INIT_LIST_HEAD( &f -> i_dentry );
	
		INIT_LIST_HEAD( &f -> i_dirty_buffers );
		INIT_LIST_HEAD( &f -> i_dirty_data_buffers );

		f -> i_ino = 42;
		atomic_set( &f -> i_count, 0 );
		f -> i_mode = 0;
		f -> i_nlink = 1;
		f -> i_uid = 201;
		f -> i_gid = 201;
		f -> i_size = 1000;
		f -> i_atime = 0;
		f -> i_mtime = 0;
		f -> i_ctime = 0;
		f -> i_blkbits = 12;
		f -> i_blksize = 4096;
		f -> i_blocks = 1;
		f -> i_mapping = &f -> i_data;
		f -> i_sb = reiser4_get_current_sb();
		sema_init( &f -> i_sem, 1 );
		init_inode( f, &coord );
		reiser4_get_object_state( f ) -> hash = hash_plugin_by_id ( DEGENERATE_HASH_ID );
		reiser4_get_object_state( f ) -> tail = tail_plugin_by_id ( NEVER_TAIL_ID );
		reiser4_get_object_state( f ) -> perm = perm_plugin_by_id ( RWX_PERM_ID );
		reiser4_get_object_state( f ) -> locality_id = get_key_locality( &key );

		print_inode( "inode", f );

		old_context = reiser4_get_current_context();
		SUSPEND_CONTEXT (old_context);

		dentry.d_name.name = ".";
		dentry.d_name.len = strlen( dentry.d_name.name );
		ret = f -> i_op -> create( f, &dentry, 0777 );
		reiser4_init_context( old_context, f -> i_sb );

		// print_tree_rec( "tree", tree, ~0u );

		dentry.d_name.name = "foo";
		dentry.d_name.len = strlen( dentry.d_name.name );
		SUSPEND_CONTEXT (old_context);
		ret = f -> i_op -> create( f, &dentry, 0777 );

		reiser4_init_context( old_context, f -> i_sb );
		info( "ret: %i\n", ret );
		SUSPEND_CONTEXT (old_context);

		dentry.d_name.name = "bar";
		dentry.d_name.len = strlen( dentry.d_name.name );
		ret = f -> i_op -> mkdir( f, &dentry, S_IFDIR | 0777 );

		reiser4_init_context( old_context, f -> i_sb );
		info( "ret: %i\n", ret );
		SUSPEND_CONTEXT (old_context);

		f -> i_op -> lookup( f, &dentry );

		reiser4_init_context( old_context, f -> i_sb );
		
		spin_lock_init( &lc_rand_guard );
		info.dir = f;
		info.num = atoi( argv[ 4 ] );
		info.max = info.num;
		info.sleep = 0;
		if( !strcmp( argv[ 2 ], "dir" ) )
			info.mkdir = 1;
		else
			info.mkdir = 0;
		if( threads > 1 ) {
			for( i = 0 ; i < threads ; ++ i )
				pthread_create( &tid[ i ], 
						NULL, mkdir_thread, &info );

			/*
			 * actually, there is no need to join them. Can either
			 * call thread_exit() here, or create them detached.
			 */
			for( i = 0 ; i < threads ; ++ i )
				pthread_join( tid[ i ], NULL );
		} else
			mkdir_thread( &info );

		print_tree_rec( "tree:dir", tree, REISER4_NODE_CHECK );

		xmemset( &df, 0, sizeof df );
		xmemset( &dd, 0, sizeof dd );

		dd.d_inode = f;
		df.f_dentry = &dd;
		df.f_op = &reiser4_file_operations;
		SUSPEND_CONTEXT (old_context);
		readdir( &df );
		reiser4_init_context( old_context, f -> i_sb );

	} else if( !strcmp( argv[ 2 ], "ibk" ) ) {
		reiser4_item_data data;
		struct {
			reiser4_stat_data_base base;
			reiser4_unix_stat      un;
		} sd;

		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			reiser4_lock_handle lh;

			reiser4_init_coord( &coord );
			reiser4_init_lh( &lh );

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
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( SD_ITEM_ID );

			ret = insert_by_key( tree, &key, &data, &coord, &lh, 
					     LEAF_LEVEL,
					     ( inter_syscall_ra_hint * )1, 0, 
					     CBK_UNIQUE );
			printf( "result: %i\n", ret );

			/* print_pbk_cache( "pbk", tree -> pbk_cache ); */
			/* print_tree( "tree", tree ); */
			info( "____end______%i_____________\n", i );

			reiser4_done_lh( &lh );
			reiser4_done_coord( &coord );

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
			carry_insert_data cdata;

			reiser4_init_carry_pool( &pool );
			reiser4_init_carry_level( &lowest_level, &pool );
		
			op = reiser4_post_carry( &lowest_level, 
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
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( SD_ITEM_ID );
			coord_first_unit( &coord );

			set_key_locality( &key, 2ull + i );

			coord.between = ( i == 0 ) ? AT_UNIT : AFTER_UNIT;
			info( "_____________%i_____________\n", i );
			print_coord( "before", &coord, 1 );
			ret = carry( &lowest_level, NULL );
			printf( "result: %i\n", ret );
			reiser4_done_carry_pool( &pool );
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
			reiser4_lock_handle lh;

			reiser4_init_coord( &coord );
			reiser4_init_lh( &lh );

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
			data.length = sizeof sd.base;
			data.iplug = item_plugin_by_id( SD_ITEM_ID );

			ret = insert_by_key( tree, &key, &data, &coord, &lh, 
					     LEAF_LEVEL,
					     ( inter_syscall_ra_hint * )1, 0, 
					     CBK_UNIQUE );
			printf( "result: %i\n", ret );

			/* print_pbk_cache( "pbk", tree -> pbk_cache ); */
			/* print_tree( "tree", tree ); */
			info( "____end______%i_____________\n", i );

			reiser4_done_lh( &lh );
			reiser4_done_coord( &coord );
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
		STYPE( inter_syscall_ra_hint );
		STYPE( reiser4_plugin_ref );
		STYPE( reiser4_plugin_ops );
		STYPE( file_plugins );
		STYPE( item_header_40 );
		STYPE( reiser4_disk_addr );
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
		STYPE( reiser4_zlock );
		STYPE( reiser4_lock_handle );
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
	struct reiser4_inode_info *rii;
	struct inode * inode;
	reiser4_item_data data;
	reiser4_key key;
	tree_coord coord;
	struct {
		reiser4_stat_data_base base;
	} sd;
	int ret;
	carry_insert_data cdata;


	reiser4_init_carry_pool( &pool );
	reiser4_init_carry_level( &lowest_level, &pool );
	
	op = reiser4_post_carry( &lowest_level,
				 COP_INSERT, root, 0 );
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
	data.length = sizeof sd.base;
	data.iplug = item_plugin_by_id( SD_ITEM_ID );
	coord_first_unit( &coord );
	
	key_init( &key );
	set_key_type( &key, KEY_SD_MINOR );
	set_key_locality( &key, 2ull );
	set_key_objectid( &key, 42ull );
	
	coord.between = AT_UNIT;
	ret = carry( &lowest_level, NULL );
	printf( "result: %i\n", ret );
	info( "_____________sd inserted_____________\n" );
	reiser4_done_carry_pool( &pool );


	rii = malloc (sizeof *rii);
	if (!rii)
		return ERR_PTR (errno);
	xmemset (rii, 0, sizeof *rii);
	inode = &rii -> vfs_inode;
	inode->i_ino = 42;
	atomic_set( &inode->i_count, 0 );
	inode->i_mode = 0;
	inode->i_nlink = 1;
	inode->i_uid = 201;
	inode->i_gid = 201;
	inode->i_size = 1000;
	inode->i_atime = 0;
	inode->i_mtime = 0;
	inode->i_ctime = 0;
	inode->i_blkbits = 12;
	inode->i_blksize = 4096;
	inode->i_blocks = 1;
	inode->i_mapping = &inode->i_data;
	inode->i_sb = reiser4_get_current_sb();
	sema_init( &inode->i_sem, 1 );
	init_inode( inode, &coord );
	reiser4_get_object_state( inode ) -> hash = hash_plugin_by_id ( DEGENERATE_HASH_ID );
	reiser4_get_object_state( inode ) -> tail = tail_plugin_by_id ( ALWAYS_TAIL_ID );
	reiser4_get_object_state( inode ) -> perm = perm_plugin_by_id ( RWX_PERM_ID );
	reiser4_get_object_state( inode ) -> locality_id = get_key_locality( &key );

	return inode;
}


/*****************************************************************************************
				      WRITE TEST
 *****************************************************************************************/

int insert_item (struct inode *inode,
		 reiser4_item_data * data,
		 reiser4_key * key)
{
	tree_coord coord;
	reiser4_lock_handle lh;
	tree_level level;
	int result;


	reiser4_init_coord (&coord);
	reiser4_init_lh (&lh);

	level = (item_plugin_id (data->iplug) == EXTENT_ITEM_ID) ? TWIG_LEVEL : LEAF_LEVEL;
	result = insert_by_key (tree_by_inode (inode), key, data, &coord, &lh,
				level, reiser4_inter_syscall_ra (inode), 0, 
				CBK_UNIQUE);

	reiser4_done_lh (&lh);
	reiser4_done_coord (&coord);
	return result;
}

static int call_create (struct inode * dir, const char * name);
static ssize_t call_write (struct inode *, const char * buf,
			   loff_t offset, unsigned count);
static ssize_t call_read (struct inode *, char * buf, 
			  loff_t offset, unsigned count);
void call_truncate (struct inode * inode, loff_t size);
static struct inode * call_lookup (struct inode * dir, const char * name);
static int call_mkdir (struct inode * dir, const char * name);



static int call_create (struct inode * dir, const char * name)
{
	reiser4_context *old_context;
	struct dentry dentry;
	int ret;


	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	dentry.d_name.name = name;
	dentry.d_name.len = strlen( name );
	ret = dir->i_op -> create( dir, &dentry, S_IFREG | 0777 );

	reiser4_init_context( old_context, dir->i_sb );
	
	return ret;
}


static ssize_t call_write (struct inode * inode, const char * buf,
			   loff_t offset, unsigned count)
{
	reiser4_context *old_context;
	ssize_t result;
	struct file file;
	struct dentry dentry;


	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	file.f_dentry = &dentry;
	dentry.d_inode = inode;
	result = inode->i_fop->write (&file, buf, count, &offset);

	reiser4_init_context (old_context, inode->i_sb);

	return result;
}


static ssize_t call_read (struct inode * inode, char * buf, loff_t offset,
			  unsigned count)
{
	reiser4_context *old_context;
	ssize_t result;
	struct file file;
	struct dentry dentry;


	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	file.f_dentry = &dentry;
	dentry.d_inode = inode;
	result = inode->i_fop->read (&file, buf, count, &offset);

	reiser4_init_context (old_context, inode->i_sb);
	return result;
}


void call_truncate (struct inode * inode, loff_t size)
{
	reiser4_context *old_context;

	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	inode->i_size = size;
	inode->i_op->truncate (inode);
	reiser4_init_context (old_context, inode->i_sb);
}


static struct inode * call_lookup (struct inode * dir, const char * name)
{
	struct dentry dentry;
	struct dentry * result;


	reiser4_context *old_context;

	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->lookup (dir, &dentry);
	reiser4_init_context (old_context, dir->i_sb);

	assert ("vs-415", ergo (result == NULL, dentry.d_inode != NULL));
	return (result == NULL) ? dentry.d_inode : ERR_PTR (PTR_ERR (result));	
}


static struct inode * call_cd (struct inode * dir, const char * name)
{
	return call_lookup (dir, name);
}


static int call_mkdir (struct inode * dir, const char * name)
{
	reiser4_context *old_context;
	struct dentry dentry;
	int result;


	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	dentry.d_name.name = name;
	dentry.d_name.len = strlen (name);
	result = dir->i_op->mkdir (dir, &dentry, S_IFDIR | 0777);

	reiser4_init_context (old_context, dir->i_sb);
	return result;
}


static int call_readdir (struct inode * dir)
{
	reiser4_context *old_context;
	struct dentry dentry;
	struct file file;


	old_context = reiser4_get_current_context();
	SUSPEND_CONTEXT( old_context );

	xmemset (&file, 0, sizeof (struct file));
	dentry.d_inode = dir;
	file.f_dentry = &dentry;
	readdir (&file);

	reiser4_init_context (old_context, dir->i_sb);
	return 0;
}


int alloc_extent (reiser4_tree *, tree_coord *,
		  reiser4_lock_handle *, void *);

#define BUFSIZE 255


/* this copies normal file @oldname to reiser4 filesystem (in directory @dir
   with name @newname) */
int copy_file (const char * oldname, 
	       struct inode * dir, const char * newname, struct stat * st)
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

	buf = malloc (BUFSIZE);
	if (!buf) {
		perror ("copy_file: malloc failed");
		return 1;
	}

	count = BUFSIZE;
	off = 0;
	while (st->st_size) {
		if ((loff_t)count > st->st_size)
			count = st->st_size;
		if (read (fd, buf, count) != (ssize_t)count) {
			perror ("copy_file: read failed");
			return 1;
		}
		if (call_write (inode, buf, off, count) != (ssize_t)count) {
			info ("copy_file: write failed\n");
			return 1;
		}
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
		st->st_size -= count;
		off += count;
	}

	printf ("\b");
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
			prefix = strlen (name);
			continue;
		}
		printf ("%s : ", name);
		
		if (!stat (name, &st)) {
			if (S_ISDIR (st.st_mode)) {
				printf ("DIR\n");
				depth = get_depth (name + prefix + 1) + 1;
				inodes = (struct inode **) realloc (inodes, sizeof (struct inode *) * depth);
				if (!inodes) {
					info ("copy_dir: realloc failed\n");
					break;
				}
				assert ("vs-344", depth > 1);

				if (call_mkdir (inodes [depth - 2], last_name (name + prefix + 1))) {
					info ("copy_dir: mkdir failed\n");
					break;
				}
				inodes [depth - 1] = call_lookup (inodes [depth - 2], last_name (name + prefix + 1));
				if (IS_ERR (inodes [depth - 1])) {
					info ("copy_dir: lookup failed\n");
					break;
				}
				dirs ++;
				/*
				 * if parent directory has tails on - make
				 * child directory to have tail off
				 */
				if (tail_plugin_id (reiser4_get_object_state (inodes [depth - 2])->tail) == NEVER_TAIL_ID)
					reiser4_get_object_state (inodes [depth - 1])->tail = tail_plugin_by_id (ALWAYS_TAIL_ID);
				else
					reiser4_get_object_state (inodes [depth - 1])->tail = tail_plugin_by_id (NEVER_TAIL_ID);

			} else if (S_ISREG (st.st_mode)) {
				printf ("REG\n");
				if (copy_file (name, inodes [depth - 1], last_name (name + prefix + 1), &st)) {
					info ("copy_dir: copy_file failed\n");
					break;
				}
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

	if (chdir (cwd)) {
		perror ("copy_dir: chdir failed");
		return 0;
	}
	free (cwd);

	info ("DONE: %d dirs, %d files\n", dirs, files);

	return 0;
}


/*
 * @full_name is name of "normal" file. @name is name of file in reiser4 tree
 * in directory @dir
 */
static void diff (const char * full_name,
		  struct inode * dir, const char * name)
{
	int fd;
	char * buf1, * buf2;
	unsigned count;
	loff_t off;
	struct inode * inode;
	struct stat st;


	if (stat (full_name, &st)) {
		perror ("diff: stat failed");
		return;
	}

	/*
	 * open file in "normal" filesystem
	 */
	fd = open (full_name, O_RDONLY);
	if (fd == -1) {
		perror ("diff: open failed");
		return;
	}

	/*
	 * lookup for the file in current directory in reiser4 tree
	 */
	inode = call_lookup (dir, name);
	if (IS_ERR (inode)) {
		info ("diff: lookup failed\n");
		return;
	}
	
	buf1 = malloc (BUFSIZE);
	buf2 = malloc (BUFSIZE);
	if (!buf1 || !buf2) {
		perror ("diff: malloc failed");
		return;
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
	return;
}


/*
 * go through all "twig" nodes and call alloc_extent for every item
 */
static void allocate_unallocated (reiser4_tree * tree)
{
	tree_coord coord;
	reiser4_lock_handle lh;
	reiser4_key key;
	int result;


	reiser4_init_coord (&coord);
	reiser4_init_lh (&lh);

	key_init (&key);
	set_key_locality (&key, 2ull);
	set_key_objectid (&key, 0x2aull);
	set_key_type (&key, KEY_SD_MINOR);
	set_key_offset (&key, 0ull);
	result = coord_by_key (tree, &key, &coord, &lh,
			       ZNODE_WRITE_LOCK, FIND_MAX_NOT_MORE_THAN,
			       TWIG_LEVEL, TWIG_LEVEL, 
			       CBK_FOR_INSERT | CBK_UNIQUE);
	coord_first_unit (&coord);
	result = reiser4_iterate_tree (tree, &coord, &lh, 
				       alloc_extent, 0, ZNODE_WRITE_LOCK, 0);

	reiser4_done_lh (&lh);
	reiser4_done_coord (&coord);

	print_tree_rec ("AFTER ALLOCATION", tree, REISER4_NODE_PRINT_HEADER |
			REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
}


static int vs_test( int argc UNUSED_ARG, char **argv UNUSED_ARG, 
		    reiser4_tree *tree )
{
	znode *root;
	struct inode * root_dir;
	int i;
	unsigned blocksize;

	blocksize = reiser4_get_current_sb ()->s_blocksize;

	root = allocate_znode( tree, 
			       allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 ),
			       tree -> height, &tree -> root_block, 0 );
	root -> ld_key = *min_key();
	root -> rd_key = *max_key();
	reiser4_sibling_list_insert( root, NULL );

	root_dir = create_root_dir (root);
	if (IS_ERR (root_dir))
		return PTR_ERR (root_dir);

	/* root directory is the only thing in the tree */

	call_create (root_dir, ".");
	/* make tree high enough */
#define NAME_LENGTH 10
	for (i = 0; tree->height < TWIG_LEVEL ; i ++) {
		char name [NAME_LENGTH];
		
		xmemset (name, '0' + i, NAME_LENGTH - 1);
		name [NAME_LENGTH - 1] = 0;
		call_create (root_dir, name);
	}

	/* to insert extent items tree must be at least this high */
	assert ("vs-359", tree->height > LEAF_LEVEL);


	if (argc == 2) {
		/* ./a.out vs */
#if 1
		{
			char name [NAME_LENGTH];
			char * buf;
			struct inode * inode;

			xmemset (name, '0', NAME_LENGTH - 1);
			name [NAME_LENGTH - 1] = 0;
		
			/*
			 * "open" file "000000000000000000000000"
			 */
			inode = call_lookup (root_dir, name);
			if (!inode || IS_ERR (inode)) {
				info ("lookup failed for file %s\n", name);
				return 0;
			}

			/*
			 * write to that file 3 blocks at offset 1 blocks
			 */
			buf = malloc (blocksize * 10);
			assert ("vs-345", buf);
			call_write (inode, buf, (loff_t)1 * blocksize, blocksize * 3);

		
			/*
			 * "open" file "11111111111111111111111"
			 */
			xmemset (name, '1', NAME_LENGTH - 1);
			name [NAME_LENGTH - 1] = 0;
			inode = call_lookup (root_dir, name);
			if (!inode || IS_ERR (inode)) {
				info ("lookup failed for file %s\n", name);
				return 0;
			}
			print_tree_rec ("AFTER 1 WRITE", tree, REISER4_NODE_PRINT_ALL);
			/*
			 * write to that file 5 blocks at offset 1 block
			 */
			call_write (inode, buf, (loff_t)1 * blocksize, blocksize * 5);

			free (buf);
			print_tree_rec ("AFTER 2 WRITES", tree, REISER4_NODE_PRINT_HEADER |
					REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
			print_pages ();
		}
#endif

		allocate_unallocated (tree);

#if 0 /* cut_tree */
		{
			reiser4_key from, to;
			char buf [NAME_LENGTH];
			struct qstr name;

		
			name.name = buf;
			name.len = NAME_LENGTH - 1;

			xmemset (buf, '4', NAME_LENGTH - 1);
			buf [NAME_LENGTH - 1] = 0;
			build_entry_key (root_dir, &name, &from);

			xmemset (buf, 'Q', NAME_LENGTH - 1);
			buf [NAME_LENGTH - 1] = 0;
			build_entry_key (root_dir, &name, &to);
			cut_tree (tree, &from, &to);

			print_tree_rec ("AFTER CUT:", tree, REISER4_NODE_PRINT_HEADER |
					REISER4_NODE_PRINT_KEYS |
					REISER4_NODE_PRINT_ITEMS);

			key_init (&from);
			set_key_locality (&from, 3ull);
			set_key_type (&from, KEY_SD_MINOR);
			set_key_objectid (&from, 0x2aull);
			set_key_offset (&from, 0ull);

			key_init (&to);
			set_key_locality (&to, 0x2aull);
			set_key_type (&to, KEY_BODY_MINOR);
			set_key_objectid (&to, 65536ull);
			/* FIXME-NIKITA cut head of extent carefully */
			set_key_offset (&to, 2 * reiser4_get_current_sb ()->s_blocksize - 1ull);

			cut_tree (tree, &from, &to);

			print_tree_rec ("AFTER SECOND CUT:", tree, REISER4_NODE_PRINT_HEADER |
					REISER4_NODE_PRINT_KEYS |
					REISER4_NODE_PRINT_ITEMS);

		}
#endif
	} else if (!strcmp (argv[2], "copydir")) {
		/*
		 * this is to be used as: find | ./a.out vs copydir
		 */
		struct inode * dir;


		call_mkdir (root_dir, "testdir");
		dir = call_lookup (root_dir, "testdir");

		copy_dir (dir);

		print_tree_rec ("AFTER COPY_DIR", tree, REISER4_NODE_PRINT_HEADER |
				REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS);
		call_readdir (dir);

		allocate_unallocated (tree);

		print_pages ();
	} else if (!strcmp (argv[2], "twig")) {
		/*
		 * test modification of search to insert empty node when no
		 * appropriate internal item is found
		 */
		struct inode * dir, * inode;

		call_mkdir (root_dir, "dir1");
		dir = call_lookup (root_dir, "dir1");
		call_create (dir, "file1");
		inode = call_lookup (dir, "file1");
		call_write (inode, "Hello, world", 0ull, strlen ("Hello, world"));

		call_mkdir (root_dir, "dir2");


		dir = call_lookup (root_dir, "dir2");
		call_create (dir, "file2");
		inode = call_lookup (dir, "file2");
		call_write (inode, "Hello, world2", 0ull, strlen ("Hello, world2"));
#if 0
		set_key_locality (&key, 35ull);
		set_key_objectid (&key, 40ull);
		set_key_type (&key, KEY_SD_MINOR);
		set_key_offset (&key, 0ull);


		item.plugin = plugin_by_id (REISER4_ITEM_PLUGIN_TYPE, SD_ITEM_ID);
		if (insert_item (root_dir, &item, &key)) {
			info ("insert_item failed for stat data\n");
			return 1;
		}
#endif

	} else if (!strcmp (argv[2], "tail")) {
		/*
		 * try to put tail items into tree
		 */
		
	} else if (!strcmp (argv[2], "bash")) {
		char * command = 0;
		size_t n = 0;
		struct inode * cwd;

		if (call_mkdir (root_dir, "testdir")) {
			info ("mkdir failed");
			return 1;
		}
		cwd = call_lookup (root_dir, "testdir");
		if (IS_ERR (cwd)) {
			info ("lookup failed");
			return 1;
		}

		while (printf (">"), getline (&command, &n, stdin) != -1) {
			/* remove ending '\n' */
			command [strlen (command) - 1] = 0;
			if (!strncmp (command, "pwd", 2)) {
				info ("Not ready\n");
			} else if (!strncmp (command, "ls", 2)) {
				call_readdir (cwd);
			} else if (!strncmp (command, "cd ", 3)) {
				/*
				 * cd
				 */
				struct inode * tmp;

				tmp = call_cd (cwd, command + 3);
				if (!tmp) {
					info ("%s failed\n", command);
					continue;
				}
				cwd = tmp;
			} else if (!strncmp (command, "mkdir ", 6)) {
				/*
				 * mkdir
				 */
				if (call_mkdir (cwd, command + 6)) {
					info ("%s failed\n", command);
					continue;
				}
			} else if (!strncmp (command, "cp ", 3)) {
				/*
				 * cp
				 */
				struct stat st;

				if (stat (command + 3, &st) || !S_ISREG (st.st_mode)) {
					errno ? perror ("stat failed") : 
						info ("%s is not regular file\n", command + 3);
					continue;
				}
				if (copy_file (command + 3, cwd, last_name (command + 3), &st)) {
					info ("%s failed\n", command);
					continue;
				}
			} else if (!strncmp (command, "diff ", 5)) {
				/*
				 * compare original with file 
				 */
				diff (command + 5, cwd, last_name (command + 5));
			} else if (!strncmp (command, "trunc ", 6)) {
				/*
				 * truncate
				 */
				struct inode * inode;

				inode = call_lookup (cwd, command + 6);
				if (IS_ERR (inode)) {
					info ("could not find file %s\n",
					      command + 6);
					continue;
				}
				info ("Current size: %Ld, new size: ",
				      inode->i_size);
				getline (&command, &n, stdin);				
				call_truncate (inode, atoll (command));
			} else if (!strncmp (command, "tail", 4)) {
				/*
				 * get tail plugin or set
				 */
				if (!strcmp (command, "tail")) {
					info (((reiser4_get_object_state (cwd)->tail ==
					      tail_plugin_by_id (NEVER_TAIL_ID)) ? "NEVER\n" : "ALWAYS\n"));
				} else if (!strcmp (command + 5, "off")) {
					reiser4_get_object_state (cwd)->tail =
						tail_plugin_by_id (NEVER_TAIL_ID);
				} else if (!strcmp (command + 5, "on")) {
					reiser4_get_object_state (cwd)->tail =
						tail_plugin_by_id (ALWAYS_TAIL_ID);
				} else {
					info ("\ttail [on|off]\n");
				}
			} else if (!strncmp (command, "p", 1)) {
				/*
				 * print tree
				 */
				print_tree_rec ("DONE", tree_by_inode (cwd),
						REISER4_NODE_PRINT_ALL & ~REISER4_NODE_PRINT_PLUGINS & ~REISER4_NODE_PRINT_ZNODE);
			} else if (!strcmp (command, "exit")) {
				/*
				 * exit
				 */
				break;
			} else
				info ("Commands:\n"
				      "\tls             - list directory\n"
				      "\tcd             - change directory\n"
				      "\tmkdir dirname  - create new directory\n"
				      "\tcp filename    - copy file to current directory\n"
				      "\tdiff filename  - compare files\n"
				      "\ttrunc filename - truncate file\n"
				      "\ttail [on|off]  - set or get state of tail plugin of current directory\n"
				      "\tp              - print tree\n"
				      "\texit\n");
		}
		info ("Done\n");
	} else {
		info ("%s vs OR %s vs copydir OR vs twig OR vs tail\n", argv [0], argv [0]);
	}

	print_tree_rec ("DONE", tree, REISER4_NODE_PRINT_HEADER |
			REISER4_NODE_PRINT_KEYS | REISER4_NODE_PRINT_ITEMS |
			REISER4_NODE_CHECK);
	return 0;
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

	if (next_key != NULL) {
		key_init           (next_key);
		set_key_objectid   (next_key, key_no + 1);
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
	id->length = sizeof (sd->base);
	id->iplug = item_plugin_by_id( SD_ITEM_ID );
}

void* monitor_test_handler (void* arg)
{
	struct super_block *super = (struct super_block*) arg;

	REISER4_ENTRY_PTR (super);

	for (;;) {
		sleep (10);

		reiser4_show_context (0);
	}

	REISER4_EXIT_PTR (NULL);
}

void* build_test_handler (void* arg)
{
	struct super_block *super = (struct super_block*) arg;
	int ret;

	for (;;) {
		reiser4_item_data      data;
		reiser4_lock_handle    lock;
		jmacd_sd               sd;
		tree_coord            coord;
		reiser4_key            key;
		__u32                  count;
		reiser4_tree          *tree;
		
		REISER4_ENTRY_PTR (super);

		tree = & reiser4_get_super_private (super)->tree;
		
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

		reiser4_init_coord (& coord);
		jmacd_key_no (& key, NULL, & sd, & data, (__u64) count);

	deadlk:
		reiser4_init_lh    (& lock);
		
		ret = insert_by_key( tree, &key, &data, &coord, &lock, 
				     LEAF_LEVEL,
				     ( inter_syscall_ra_hint * )1, 0, CBK_UNIQUE );

		reiser4_done_lh (& lock);

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
		reiser4_lock_handle    lock;
		jmacd_sd               sd;
		tree_coord             coord;
		reiser4_key            key, next_key;
		reiser4_tree          *tree;
		__u32                  item;
		__u32                  exists;
		__u32                  opc;
		
		REISER4_ENTRY_PTR (super);

		tree = & reiser4_get_super_private (super)->tree;
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

		reiser4_init_coord (& coord);
		jmacd_key_no       (& key, & next_key, & sd, & data, (__u64) item);

	deadlk:
		if (exists == 0) {

			reiser4_init_lh (& lock);

			ret = insert_by_key( tree, &key, &data, &coord, &lock, 
					     LEAF_LEVEL,
					     ( inter_syscall_ra_hint * )1, 0, CBK_UNIQUE );

			reiser4_done_lh (& lock);

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

	proc_ids              = malloc (sizeof (pthread_t) * procs);
	_jmacd_exists_map     = malloc (_jmacd_items);

	for (i = 0; i < _jmacd_items; i += 1) {
		_jmacd_exists_map[i] = 1;
	}

	/* These four magic lines are taken from nikita_test, and seem to be
	 * necessary--maybe they belong somewhere else... */
	fake = allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR, 1 );
	root = allocate_znode( tree, fake, tree -> height, &tree -> root_block, 
			       !strcmp( argv[ 2 ], "mkr4fs" ) );
	root -> rd_key = *max_key();
	reiser4_sibling_list_insert( root, NULL );

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
				      
 *****************************************************************************************/

typedef struct {
	const char *name;
	int ( * func )( int argc, char **argv, reiser4_tree *tree );
} tester;

static tester team[] = {
	{
		.name = "vs",
		.func = vs_test
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
		.name = NULL,
		.func = NULL
	},
};

extern int init_inodecache( void );

void funJustBeforeMain()
{}

int real_main( int argc, char **argv )
{
	int result, eresult, fresult;
	struct super_block super;
	struct dentry root_dentry;
	reiser4_tree *tree;
	reiser4_disk_addr root_block;
	int tree_height;

	REISER4_ENTRY( &super );

	__prog_name = strrchr( argv[ 0 ], '/' );
	if( __prog_name == NULL )
		__prog_name = argv[ 0 ];
	else
		++ __prog_name;
 	abendInit( argc, argv );

	/*trap_signal( SIGBUS );
	  trap_signal( SIGSEGV );*/

	if( getenv( "REISER4_TRACE_FLAGS" ) != NULL ) {
		reiser4_current_trace_flags = 
			strtol( getenv( "REISER4_TRACE_FLAGS" ), NULL, 0 );
		rlog( "nikita-1496", "reiser4_current_trace_flags: %x", 
		      reiser4_get_current_trace_flags() );
	}

	init_inodecache();
	znodes_init();
	init_plugins();
	txn_init_static();
	sys_rand_init();
	xmemset( &super, 0, sizeof super );
	super.s_blocksize = getenv( "REISER4_BLOCK_SIZE" ) ? 
		atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
	assert( "vs-417", super.s_blocksize == 512 ||
		super.s_blocksize == 1024 || super.s_blocksize == 2048 ||
		super.s_blocksize == 4096);

	super.s_op = &reiser4_super_operations;
	super.s_root = &root_dentry;

	xmemset( &reiser4_get_current_super_private() -> stats, 0, 
		sizeof reiser4_get_current_super_private() -> stats );
	txn_mgr_init( &reiser4_get_super_private (&super) -> tmgr );

	root_dentry.d_inode = NULL;
	reiser4_init_oid_allocator( reiser4_get_oid_allocator( &super ) );

	INIT_LIST_HEAD( &inode_hash_list );
	INIT_LIST_HEAD( &page_list );

	root_block.blk = 3ull;
	tree_height = 1;
	if( getenv( "REISER4_UL_DURABLE_MMAP" ) != NULL ) {
		mmap_back_end_fd = open( getenv( "REISER4_UL_DURABLE_MMAP" ),
					 O_CREAT | O_RDWR, 0700 );
		if( mmap_back_end_fd == -1 ) {
			fprintf( stderr, "%s: Cannot open %s: %s\n", argv[ 0 ],
				 getenv( "REISER4_UL_DURABLE_MMAP" ),
				 strerror( errno ) );
			exit( 1 );
		}
		mmap_back_end_size = lseek64( mmap_back_end_fd, (off_t)0, SEEK_END );
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
		if( pread( mmap_back_end_fd, &root_block, sizeof root_block, (off_t)0 ) != sizeof root_block ) {
			perror( "read root block" );
			exit( 4 );
		}
		if( root_block.blk == 0 )
			root_block.blk = 3;
		if( pread( mmap_back_end_fd, &tree_height, sizeof tree -> height, (off_t)(sizeof root_block) ) != sizeof tree -> height ) {
			perror( "read tree height" );
			exit( 4 );
		}
		if( tree_height == 0 )
			tree_height = 1;
	}

	tree = &reiser4_get_super_private( &super ) -> tree;
	result = reiser4_init_tree( tree, &root_block,
				    1, node_plugin_by_id( NODE40_ID ),
				    ulevel_read_node );
	tree -> height = tree_height;

	if( result )
		rpanic ("jmacd-500", "znode_tree_init failed");

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
	if( ( mmap_back_end_fd > 0 ) && 
	    ( pwrite( mmap_back_end_fd, &tree -> root_block, 
		      sizeof root_block, (off_t)0 ) != sizeof root_block ) ) {
			perror( "write root block" );
			exit( 5 );
		}
	if( ( mmap_back_end_fd > 0 ) && 
	    ( pwrite( mmap_back_end_fd, &tree -> height, sizeof tree -> height, (off_t)(sizeof root_block) ) != sizeof tree -> height ) ) {
			perror( "write tree height" );
			exit( 4 );
	}
	info( "tree height: %i\n", tree -> height );

	eresult = __REISER4_EXIT( &__context );
	fresult = txn_mgr_force_commit (& super);

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
	reiser4_init_context_mgr();

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
