/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation.
 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "ulevel.h"
#include "../reiser4.h"
#include "../kcond.h"
#include "../inode.h"
#include "../super.h"
#include "../key.h"
#include "../dformat.h"

#include "../pool.h"
#include "../carry.h"
#include "../carry_ops.h"

#include <pthread.h>

void panic( const char *format, ... )
{
	char *panic_mode;
	va_list args;

	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
	panic_mode = getenv( "REISER4_CRASH_MODE" );
	if( !panic_mode )
		abort();
	else if( !strcmp( panic_mode, "debugger" ) )
		abend();
	else if( !strcmp( panic_mode, "suspend" ) ) {
		while( 1 )
		{;}
	}
}

int sema_init( semaphore *sem, int value )
{
	pthread_mutex_init( &sem -> mutex, NULL );
	if( value == 0 )
		pthread_mutex_lock( &sem -> mutex );
	return 0;
}

int init_MUTEX( semaphore *sem )
{
	return sema_init( sem, 1 );
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
			reiser4_panic("jmacd-1000", "down() too long!");
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

void dump_stack( void )
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

void spinlock_bug (const char *msg)
{
	reiser4_panic("jmacd-1010", "spinlock: %s", msg);
}


static void ul_init_file(struct file *f)
{
	assert("nikita-2860", f != NULL);
}

#define KMEM_CHECK 1
#define KMEM_MAGIC 0x74932123U

#define KMEM_FAILURES (1)
#if KMEM_FAILURES
static int kmalloc_failure_rate = 0;
#endif

static __u64 total_allocations = 0ull;


/* addr was allocated by xxmalloc. Get size of that area */
static size_t xx_get_size (void * addr)
{
	__u32 *check = addr;

	return *(check - 1);
}

/* check area allocated by xxmalloc */
static void xx_check_mem (void * addr)
{
	__u32 *check = addr;
	size_t size;

	size = xx_get_size (addr);
	assert( "jmacd-1065", *(check - 2) == KMEM_MAGIC);
	assert ("vs-818", *(__u32 *)((char *)check + size) == KMEM_MAGIC);
}


pthread_t uswapper;
kcond_t memory_pressed;
int going_down;
spinlock_t mp_guard;
int is_mp;
/* this is used to wait in xxmalloc until usswapd released some memory */
int is_mp_done;
kcond_t memory_pressure_done;

static void *xxmalloc( size_t size )
{
	__u32 * addr;

	if( KMEM_FAILURES && ( rand() < kmalloc_failure_rate ) ) {
		printk( "xxmalloc failed at its discretion\n" );
		return NULL;
	}

	while ( getenv ("REISER4_SWAPD") &&
		total_allocations > MEMORY_PRESSURE_THRESHOLD &&
		!current -> i_am_swapd ) {
		/* wakeup uswapd */
		spin_lock( &mp_guard );
		is_mp = 1;
		is_mp_done = 0;
		spin_unlock( &mp_guard );
		kcond_broadcast( &memory_pressed );

		if (/*BLOCKING_MEMORY_PRESSURE*/ 0) {
			/* wait until it is done */
			spin_lock( &mp_guard );
			while( !is_mp_done )
				kcond_wait( &memory_pressure_done, &mp_guard, 0 );
			spin_unlock( &mp_guard );
		} else {
			/* Josh's hack: screw memory pressure, let's make progress! */
			break;
		}
	}

	addr = malloc( size + sizeof (__u32) * 3 );
	if (addr) {
		total_allocations += size;
		*addr = KMEM_MAGIC;
		*(addr + 1) = size;
		*((__u32 *) ((char *)addr + size + sizeof (__u32) * 2)) = KMEM_MAGIC;
		addr += 2;
		xx_check_mem (addr);
	}
	return addr;
}

static void xxfree( void *addr )
{
	xx_check_mem (addr);

	assert ("vs-819", total_allocations >= xx_get_size (addr));
	total_allocations -= xx_get_size (addr);
//	memset( addr, 0xf0, xx_get_size (addr) );
	free( (char *)addr - sizeof (__u32) * 2);
}

void *kmalloc( size_t size, int flag UNUSE )
{
	return xxmalloc( size );
}

void kfree( void *addr )
{
	if( addr != NULL )
		xxfree( addr );
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
				 void (*ctor)(void*, kmem_cache_t *, unsigned long),
				 void (*dtor)(void*, kmem_cache_t *, unsigned long) )
{
	kmem_cache_t *result;

	result = kmalloc( sizeof *result, 0 );
	result -> size  = size;
	result -> count = 0;
	result -> name  = name;
	result -> ctor  = ctor;
	result -> dtor  = dtor;
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

	if( slab -> ctor != NULL )
		slab -> ctor( addr, slab, 0 );

	kfree( addr );

	spin_lock (& slab -> lock);
	if (slab -> count == 0) {
		reiser4_panic("jmacd-1066", "%s slab allocator: too many frees", slab -> name);
	}
	slab -> count -= 1;
	spin_unlock (& slab -> lock);
}

/*
    Audited by umka (2002.06.13)
    xmemset may try access NULL addr in the case kmalloc
    will unable to allocate specified size.
*/
void *kmem_cache_alloc( kmem_cache_t *slab, int gfp_flag )
{
	void *addr;

	assert("nikita-3045", schedulable());

	addr = kmalloc( slab -> size, gfp_flag );

	if (addr) {
		xmemset( addr, 0, slab -> size );
		spin_lock (& slab -> lock);
		slab -> count += 1;
		spin_unlock (& slab -> lock);
		if( slab -> ctor != NULL )
			slab -> ctor( addr, slab, SLAB_CTOR_CONSTRUCTOR );
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

int unregister_filesystem(struct file_system_type * fs)
{
	assert( "nikita-2117", fs == file_systems[ 0 ] );
	file_systems[ 0 ] = NULL;
	return 0;
}

struct super_block super_blocks[1];
struct request_queue rq;
struct block_device block_devices[1];

struct super_block * get_sb_bdev (struct file_system_type *fs_type,
				  int flags, char *dev_name,
				  void * data,
				  int (*fill_super)(struct super_block *, void *, int))
{
	struct super_block * s;
	int result;

	s = &super_blocks[0];
	s->s_flags = flags;
	s->s_blocksize = PAGE_CACHE_SIZE;
	s->s_type = fs_type;
	s->s_blocksize_bits = PAGE_CACHE_SHIFT;
	s->s_bdev = &block_devices[0];
	s->s_bdev->bd_dev = open (dev_name, O_RDWR);
	s->s_bdev->last_sector = 0ull;
	if (s->s_bdev->bd_dev == -1)
		return ERR_PTR (-errno);
	rq.max_sectors = 256;
	s->s_bdev->bd_queue = &rq;


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



static struct super_block * call_mount (const char * dev_name, const char *opts)
{
	struct file_system_type * fs;

	if (opts != NULL) {
		info ("mount options: %s\n", opts);
	}

	fs = find_filesystem ("reiser4");
	if (!fs)
		return 0;

	return fs->get_sb (fs, 0/*flags*/, (char *)dev_name, (char *)opts);
}

static struct inode *get_root_dir( struct super_block *s )
{
	return reiser4_iget (s, get_super_private (s)->df_plug->root_dir_key (s));
}


/* all inodes are in one list **/
static spinlock_t inode_hash_guard = SPIN_LOCK_UNLOCKED;
struct list_head inode_hash_list;


#if 0
/****************************************************************************/
/* hash table support */

#define PAGE_HASH_TABLE_SIZE 8192

typedef struct page *page_p;

static inline int indexeq( const page_p *p1,
			   const page_p *p2 )
{
	return
		( ( *p1 ) -> index == ( *p2 ) -> index ) &&
		( ( *p1 ) -> mapping == ( *p2 ) -> mapping );
}

static inline __u32 indexhashfn( const page_p *p )
{
	__u32 result;

	result = ( ( __u32 ) ( *p ) -> mapping ) << 16;
	result = result | ( ( *p ) -> index & 0xffff );
	return result & ( PAGE_HASH_TABLE_SIZE - 1 );
}

/** The hash table definition */
#define KMALLOC( size ) malloc( size )
#define KFREE( ptr, size ) free( ptr )
TS_HASH_DEFINE( pc, struct page, page_p, self, link, indexhashfn, indexeq );
#undef KFREE
#undef KMALLOC

#endif


/** definition of hash table of address space. radix_tree_insert|lookup|delete
 * deal with it */
static inline int indexeq (const unsigned long *p1, const unsigned long *p2 )
{
	return *p1 == *p2;
}

#define MAPPING_HASH_TABLE_SIZE 32

static inline __u32 indexhashfn( const unsigned long *p )
{
	__u32 result;

	result = ( ( *p ) & 0xffff );
	return result & ( MAPPING_HASH_TABLE_SIZE - 1 );
}

#define KMALLOC( size ) malloc( size )
#define KFREE( ptr, size ) free( ptr )
TS_HASH_DEFINE( mp, struct page, unsigned long, index, link, indexhashfn, indexeq );
#undef KFREE
#undef KMALLOC


static struct inode * alloc_inode (struct super_block * sb)
{
	struct inode * inode;

	inode = sb->s_op->alloc_inode(sb);
	if (inode == NULL)
		return NULL;
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
	spin_lock_init (&inode->i_mapping->page_lock);

	INIT_LIST_HEAD (&inode->i_mapping->private_list);
	spin_lock_init(&inode->i_mapping->private_lock);
	inode->i_mapping->assoc_mapping = NULL;

	/*
	 * FIXME-VS: init mapping's hash table of pages
	 */
	inode->i_mapping->page_tree.vp = (mp_hash_table *)xxmalloc (sizeof (mp_hash_table));
	assert ("vs-807", inode->i_mapping->page_tree.vp);
	mp_hash_init ((mp_hash_table *)(inode->i_mapping->page_tree.vp), MAPPING_HASH_TABLE_SIZE);

	return inode;
}


struct inode * new_inode (struct super_block * sb)
{
	struct inode * inode;

	inode = alloc_inode (sb);
	if (inode == NULL)
		return NULL;
	inode->i_nlink = 1;
	spin_lock( &inode_hash_guard );
	list_add (&inode->i_hash, &inode_hash_list);
	spin_unlock( &inode_hash_guard );

	init_rwsem( &reiser4_inode_data( inode ) -> sem );
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
		if (!test || !test(inode, data))
			continue;
		return inode;
	}
	return 0;
}

/*
 * Unlike the various iget() interfaces, find_get_inode only returns an inode if it is found in the inode cache.
 */
struct inode * ilookup5(struct super_block * sb, unsigned long ino, int (*test)(struct inode *, void *), void *data)
{
	struct inode *result;
	spin_lock(&inode_hash_guard);
	result = find_inode (sb, ino, test, data);
	if (result != NULL) {
		atomic_inc (& result->i_count);
	}
	spin_unlock(&inode_hash_guard);
	return result;
}

static void truncate_inode_pages (struct address_space * mapping,
				  loff_t from);

int shrink_icache (void)
{	
	struct list_head * cur, * tmp;
	struct inode * inode;
	int removed;

	removed = 0;
	spin_lock (&inode_hash_guard);
	list_for_each_safe (cur, tmp, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		if (atomic_read (&inode->i_count) || (inode->i_state & I_DIRTY))
			continue;
		truncate_inode_pages (inode->i_mapping, (loff_t)0);
		list_del_init (&inode->i_hash);
		inode->i_sb->s_op->destroy_inode (inode);		
		removed ++;
	}
	spin_unlock (&inode_hash_guard);
	info ("shrink_icache: removed %d inodes\n", removed);
	return 0;
}


/* remove all inodes from their list */
int invalidate_inodes (struct super_block *sb)
{
	struct list_head * cur, * tmp;
	struct inode * inode;
	int busy;

	busy = 0;
	list_for_each_safe (cur, tmp, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		if (inode->i_sb != sb)
			continue;
		/*truncate_inode_pages (inode->i_mapping, (loff_t)0);*/
		if (atomic_read (&inode->i_count) || (inode->i_state & I_DIRTY)){
			/* print_inode ("invalidate_inodes", inode); */
			++ busy;
		}
		spin_lock (&inode_hash_guard);
		list_del_init (&inode->i_hash);
		inode->i_sb->s_op->destroy_inode (inode);
		spin_unlock (&inode_hash_guard);
	}
	return busy;
}

void generic_delete_inode(struct inode *inode)
{
	struct super_operations *op = inode->i_sb->s_op;

	/* destroy inode */
	list_del_init( &inode -> i_hash );
	spin_unlock( &inode_hash_guard );

	/* file is deleted. free all its pages */
	truncate_inode_pages (inode->i_mapping, (loff_t)0);

	/* delete file from the tree */
	/*
	 * FIXME-VS: reiser4 does not have delete inode!
	 */

	if (op->delete_inode)
		op->delete_inode (inode);
	else
		clear_inode(inode);

	op->destroy_inode (inode);
}

void generic_forget_inode(struct inode *inode)
{
	/* last reference to file is closed, call release */
	struct file file;
	struct dentry dentry;
			
	spin_unlock( &inode_hash_guard );
	/*
	 * we don't have struct file in the user level. Emulate close of last
	 * struct file here.
	 */
	xmemset (&dentry, 0, sizeof dentry);
	xmemset (&file, 0, sizeof file);
	file.f_dentry = &dentry;
	dentry.d_inode = inode;
	if (inode->i_fop && inode->i_fop->release &&
	    inode->i_fop->release (inode, &file))
		info ("release failed\n");
	/* leave inode in hash table with all its pages */
}

void generic_drop_inode(struct inode *inode)
{
	if (!inode->i_nlink)
		generic_delete_inode(inode);
	else
		generic_forget_inode(inode);
}

static inline void iput_final( struct inode *inode )
{
	struct super_operations *op = inode->i_sb->s_op;
	void (*drop)(struct inode *) = generic_drop_inode;

	if (op && op->drop_inode)
		drop = op->drop_inode;
	drop(inode);
}

void iput( struct inode *inode )
{
	struct super_operations *op;

	if( !inode )
		return;
	op = inode->i_sb->s_op;

	if (op && op->put_inode)
		op->put_inode(inode);

	if( atomic_dec_and_lock( &inode -> i_count, &inode_hash_guard ) )
		iput_final( inode );
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


static struct inode *
get_new_inode(struct super_block *sb,
	      unsigned long hashval,
	      int (*test)(struct inode *, void *),
	      int (*set)(struct inode *, void *), void *data)
{
	struct inode * inode;

	inode = alloc_inode(sb);
	if (inode == NULL)
		return NULL;
		
	init_rwsem( &reiser4_inode_data( inode ) -> sem );
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
			/*
			 * FIXME-VS: put inode into hash list
			 */
			list_add (&inode->i_hash, &inode_hash_list);
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
		assert ("vs-827", self);
		memset (self, 0, sizeof (struct task_struct));
		self->fs_context = 0;
		self->sig = &self->sig_here;
		spin_lock_init (&self->sig->siglock);
		if ((ret = pthread_setspecific (__current_key, self)) != 0) {
			reiser4_panic("jmacd-900", "pthread_setspecific failed");
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



/* lib/radix_tree.c */

/*
 * radix_tree_insert, radix_tree_lookup, radix_tree_delete are used to attach,
 * look for and detach page from mapping. Mapping has struct field page_tree
 * (of type struct radix_tree_root) which is used to attach pages to the
 * mapping. When page is attached to mapping - it gets reference counter
 * incremented
 */
void * radix_tree_lookup (struct radix_tree_root * tree, unsigned long index)
{
	struct page * page;

	page = mp_hash_find ((mp_hash_table *)tree->vp, &index);
	return page;
}


int radix_tree_insert (struct radix_tree_root * tree,
		       unsigned long index UNUSED_ARG,
		       void * item)
{
	mp_hash_insert ((mp_hash_table *)tree->vp, (struct page *)item);
	return 0;
}


int radix_tree_delete (struct radix_tree_root * tree, unsigned long index)
{
	struct page * page;

	page = (struct page *)radix_tree_lookup (tree, index);
	assert ("vs-808", page);
	mp_hash_remove ((mp_hash_table *)tree->vp, page);
	return 0;
}


/* mm/filemap.c */

static int check_list_head( struct list_head *entry )
{
	return
		( entry != NULL ) &&
		( entry -> next != NULL ) && ( entry -> prev != NULL ) &&
		( entry -> next -> prev == entry ) &&
		( entry -> prev -> next == entry );
}

static int check_list_head_list( struct list_head *list )
{
	struct list_head *entry;

	list_for_each( entry, list )
		if( !check_list_head( entry ) )
			return 0;
	return 1;
}

/* all pages are on this list. uswapd scans this list, writeback-s and frees
 * pages */
struct list_head page_lru_list;
static spinlock_t page_list_guard = SPIN_LOCK_UNLOCKED;
unsigned long nr_pages;


static void init_page (struct page * page)
{
	page->private = 0;
	page->self = page;
	atomic_set (&page->count, 1);
	/* use kmap to set this */
	page->virtual = 0;
	spin_lock_init (&page->lock);
	spin_lock_init (&page->lock2);

}


/* include/linux/pagemap.h */
struct page * page_cache_alloc (struct address_space * mapping UNUSED_ARG)
{
	struct page * page;


	spin_lock( &page_list_guard );

	page = kmalloc (sizeof (struct page) + PAGE_CACHE_SIZE, 0);
	assert ("vs-790", page);
	xmemset (page, 0, sizeof (struct page) + PAGE_CACHE_SIZE);
	atomic_set (&page->count, 1);
	
	init_page (page);

	assert ("nikita-2658", check_list_head_list (&page_lru_list));
	/* add page into global lru list */
	list_add (&page->lru, &page_lru_list);
	nr_pages ++;

	spin_unlock( &page_list_guard );
	return page;
}


void lock_page (struct page * p)
{
	spin_lock (&p->lock);
	SetPageLocked (p);
	assert ("vs-287", PageLocked (p));
}


void unlock_page (struct page * p)
{
	assert ("vs-286", PageLocked (p));
	ClearPageLocked (p);
	spin_unlock (&p->lock);
}


struct page * find_get_page (struct address_space * mapping,
			     unsigned long ind)
{
	struct page * page;

	read_lock (&mapping->page_lock);

	page = radix_tree_lookup (&mapping->page_tree, ind);
	if (page) {
		page_cache_get (page);
	}
	read_unlock (&mapping->page_lock);
	return page;
/*
	struct page * page;
	struct {
		unsigned long index;
		struct address_space *mapping;
	} pkey = { .index = ind, .mapping = mapping };
	page_p pkey_p = ( page_p ) &pkey;

	spin_lock( &page_list_guard );
	page = pc_hash_find( &page_htable, &pkey_p );
	if( page != NULL )
		atomic_inc (&page->count);
	spin_unlock( &page_list_guard );
	return page;
*/
}


struct page * find_lock_page (struct address_space * mapping,
			      unsigned long ind)
{
	struct page * page;

	read_lock (&mapping->page_lock);
 repeat:
	page = radix_tree_lookup (&mapping->page_tree, ind);
	if (page) {
		page_cache_get (page);
		read_unlock (&mapping->page_lock);
		lock_page (page);
		read_lock (&mapping->page_lock);
		if (page->mapping != mapping || page->index != ind) {
			unlock_page (page);
			page_cache_release (page);
			goto repeat;
		}			
	}

	read_unlock (&mapping->page_lock);
	return page;
}


struct page * find_or_create_page (struct address_space * mapping,
				   unsigned long ind, int gfp)
{
	return grab_cache_page(mapping, ind);
}

/* this increases page->count and locks the page */
struct page * grab_cache_page (struct address_space * mapping,
			       unsigned long ind)
{
	struct page * page;

 repeat:
	page = find_lock_page (mapping, ind);
	if (page)
		return page;

	page = page_cache_alloc (mapping);
	if (page) {
		if (add_to_page_cache_unique (page, mapping, ind))
			goto repeat;
	}
	return page;
}


void remove_inode_page (struct page * page)
{
	/* remove page from mapping */
	radix_tree_delete (&page->mapping->page_tree, page->index);
	/* remove from mapping's list: clean, dirty or locked */
	list_del_init (&page->list);
	page->mapping->nrpages--;
	page->mapping = NULL;
}

#if 0
static void truncate_inode_pages2 (struct address_space * mapping,
				   loff_t from)
{
	struct page *  tmp;
	struct page *  page;
	struct page ** bucket;
	unsigned ind;

	ind = (from + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	for_all_in_htable( &page_htable, page, page, tmp ) {
		if (page->mapping == mapping) {
			if (page->index >= ind) {
				spin_lock (&page->lock2);
				atomic_inc (&page->count);
				mapping->a_ops->invalidatepage (page, 0);
				remove_inode_page (page);
				assert ("jmacd-8743",  atomic_read (&page->count) > 0);
				/* FIXME: this is questionable, however it currently
				 * doesn't cause problems because jnodes never release
				 * their pages, thus there is an extra jnode reference to
				 * dec here and the above assertion never fails. */
				atomic_dec (&page->count);
				if (!atomic_read (&page->count)) {
					spin_lock( &page_list_guard );
					pc_hash_remove( &page_htable, page );
					spin_unlock( &page_list_guard );
					spin_unlock (&page->lock2);
					free (page);
					continue;
				}
				spin_unlock (&page->lock2);
			}
		}
	}
}
#endif

static void truncate_complete_page (struct page * page)
{
	page->mapping->a_ops->invalidatepage (page, 0);
	ClearPageDirty (page);
	ClearPageUptodate (page);
	remove_inode_page (page);
	page_cache_release (page);
}


int truncate_list_pages (struct address_space * mapping,
			 struct list_head *head,
			 unsigned long start)
{
	struct list_head * cur, * tmp;
	struct page * page;

 repeat:
	list_for_each_safe (cur, tmp, head) {
		int failed;

		page = list_entry (cur, struct page, list);
		if (page->index < start) {
			continue;
		}
		page_cache_get (page);
		failed = PageLocked (page);
		if (failed) {
			write_unlock (&mapping->page_lock);
			wait_on_page_locked (page);
			page_cache_release (page);
			write_lock (&mapping->page_lock);
			goto repeat;
		}
		lock_page (page);
		if (PageWriteback(page)) {
			unlock_page (page);
			write_unlock (&mapping->page_lock);
			wait_on_page_writeback (page);
			assert ("vs-810", !PageWriteback (page));
			page_cache_release (page);
			write_lock (&mapping->page_lock);
			goto repeat;
		}

		truncate_complete_page (page);

		unlock_page (page);
		page_cache_release (page);
	}
	return 0;
}


/* remove pages which are beyond offset @from */
static void truncate_inode_pages (struct address_space * mapping,
				  loff_t from)
{
	unsigned ind;

	
	ind = (from + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	// write_lock (&mapping->page_lock);

	truncate_list_pages (mapping, &mapping->clean_pages, ind);
	truncate_list_pages (mapping, &mapping->dirty_pages, ind);
	truncate_list_pages (mapping, &mapping->locked_pages, ind);

	// write_unlock (&mapping->page_lock);
}


/* look for page in cache, if it does not exist - create a new one and
   call @filler */
struct page *read_cache_page (struct address_space * mapping,
			      unsigned long idx,
			      int (* filler)(void *, struct page *),
			      void *data)
{
	int result;
	struct page * page;


 repeat:
	page = find_get_page (mapping, idx);
	if (page) {
		if (PageUptodate(page)) {
			return page;
		}
		lock_page (page);
	} else {
		page = page_cache_alloc (mapping);
		if (!page)
			return ERR_PTR (-ENOMEM);
		if (add_to_page_cache_unique (page, mapping, idx))
			goto repeat;
	}

	result = filler (data, page);
	if (result)
		return ERR_PTR (result);

	return page;
}

int generic_file_mmap(struct file * file UNUSED_ARG,
		      struct vm_area_struct * vma UNUSED_ARG)
{
	return 0;
}


/* mm/filemap.c:add_to_page_cache_unique
   look for page in mapping's "page_tree"
   if it is not found:
   	add page into it
	increase page reference counter
	set Lock bit
	clear Dirty bit
	add page into mapping's list of clean pages
*/
int add_to_page_cache_unique (struct page * page,
			      struct address_space * mapping,
			      unsigned long offset)
{
	write_lock (&mapping->page_lock);

	if (radix_tree_lookup (&mapping->page_tree, offset)) {
		write_unlock (&mapping->page_lock);
		return -EEXIST;
	}

	page->index = offset;
	radix_tree_insert (&mapping->page_tree, offset, page);
	mapping->nrpages ++;

	page_cache_get (page);
	page->mapping = mapping;
	list_add (&page->list, &mapping->clean_pages);
	lock_page (page);
	ClearPageDirty (page);

	write_unlock (&mapping->page_lock);
	return 0;
}

int add_to_page_cache (struct page * page,
		       struct address_space * mapping,
		       unsigned long offset)
{
	/* FIXME: JMACD->VS: Is this right? */
	return add_to_page_cache_unique (page, mapping, offset);
}

void wait_on_page_locked(struct page * page)
{
	reiser4_stat_file_add (wait_on_page);
	/* SetPageUptodate (page); */
	/* FIXME:NIKITA->VS why kunmap? */
	/* kunmap (page); */
	lock_page (page);
	unlock_page (page);
}


void wait_on_page_writeback(struct page * page UNUSED_ARG)
{
	assert ("vs-821", !PageWriteback (page));
	return;
}


/* mm/readahead.c */
#define READAHEAD_SIZE 64

/*
 * FIXME-VS: currently, address_space does not have do_page_cache_readahead
 * method. Call it directly, then.
 */
int reiser4_do_page_cache_readahead (struct file * file,
				     unsigned long start_page,
				     unsigned long intrafile_readahead_amount);

void page_cache_readahead(struct address_space *mapping,
			  struct file_ra_state *ra,
			  struct file *filp, unsigned long offset)
{
}


#if 0
static void invalidate_pages (void)
{
	struct page *  tmp;
	struct page *  page;
	struct page ** bucket;

	spin_lock( &page_list_guard );
	for_all_in_htable( &page_htable, page, page, tmp ) {
		spin_lock (&page->lock2);
		lock_page (page);
		assert ("vs-666", !PageDirty (page));
		assert ("vs-667", atomic_read (&page->count) == 0);
		assert ("vs-668", !page->kmap_count);
		pc_hash_remove( &page_htable, page );
		spin_unlock (&page->lock2);
		free (page);
	}
	spin_unlock( &page_list_guard );
}
#endif

/*
void print_pages (const char *prefix)
{
	struct page *  tmp;
	struct page *  page;
	struct page ** bucket;

	for_all_in_htable( &page_htable, bucket, page, tmp, link )
		print_page( prefix, page );
}
*/

#if REISER4_DEBUG_OUTPUT
void print_inodes (const char *prefix)
{
	struct list_head * cur;
	struct inode * inode;

	list_for_each (cur, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		print_inode (prefix, inode);
	}
}


/* print number of pages atached to inode's mapping */
static void print_inode_pages (struct inode * inode)
{
	struct address_space * mapping;
	struct page *  tmp;
	struct page *  page;


	mapping = &inode->i_data;
	read_lock (&mapping->page_lock);
	info ("\tPAGES %ld:\n", mapping->nrpages);


	for_all_in_htable ((mp_hash_table *)mapping->page_tree.vp,
			   mp, page, tmp) {
		info ("\t%p, index %lu, count %d, flags (%s, %s, %s)\n",
		      page, page->index, atomic_read (&page->count),
		      PageLocked (page) ? "locked" : "not locked",
		      PageDirty (page) ? "dirty" : "clean",
		      PageUptodate (page) ? "uptodate" : "not uptodate");
		if (PagePrivate (page)) {
			info_jnode ("\tjnode", (jnode *)page->private);
			info ("\n");
		}
	}

	read_unlock (&mapping->page_lock);
}


/* print all the inodes and pages attached to them */
void print_inodes_2 (void)
{
	struct inode * inode;
	struct list_head * cur;

	list_for_each (cur, &inode_hash_list) {
		inode = list_entry (cur, struct inode, i_hash);
		info ("%p: INO %lu: (count=%d)\n", inode, inode->i_ino, atomic_read (&inode->i_count));
		print_inode_pages (inode);
	}
}
#endif

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
	spin_lock (&page->lock2);
	/*assert ("vs-724", page->kmap_count > 0);*/
	page->kmap_count --;
	if (page->kmap_count == 0) {
		page->virtual = 0;
/*  		fprintf (stderr, "[%i]: page: %p, node: %p (%p:%p:%p:%p:%p)\n", */
/*  			 current->pid, page, jnode_by_page (page), */
/*  			 getFrame (0), getFrame (1),  */
/*  			 getFrame (2), getFrame (3), getFrame (4) ); */
	}
	spin_unlock (&page->lock2);
}

unsigned long get_jiffies ()
{
	struct timeval tv;
	if (gettimeofday (&tv, NULL) < 0) {
		reiser4_panic ("jmacd-1001", "gettimeofday failed");
	}
	/* Assume a HZ of 1e6 */
	return (tv.tv_sec * 1e6 + tv.tv_usec);
}


void page_cache_get(struct page * page)
{
	atomic_inc (&page->count);
}


/* mm/page_alloc.c */
void page_cache_release (struct page * page)
{
	assert ("vs-352", page_count (page) > 0);
	atomic_dec (&page->count);
	if (!page_count (page)) {
		list_del_init (&page->lru);
		kfree (page);
	}
}


/* fs/buffer.c */
int fsync_bdev(struct block_device * bdev UNUSED_ARG)
{
#if 0
	struct page *  tmp;
	struct page *  page;
	struct page ** bucket;


	for_all_in_htable( &page_htable, bucket, page, tmp, link ) {
		struct buffer_head bh, *pbh;
		jnode *j;

		if (!PageDirty (page))
			continue;
		impossible ("vs-747", "still dirty pages?");
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
#endif
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
request_queue_t *bdev_get_queue(struct block_device *bdev)
{
	return bdev->bd_queue;
}

#if !REISER4_DEBUG
typedef long long int off64_t;
#endif

void ll_rw_block (int rw, int nr, struct buffer_head ** pbh)
{
	int i;
	loff_t offset;

	for (i = 0; i < nr; i ++) {
		offset = pbh[i]->b_size * (off64_t)pbh[i]->b_blocknr;
		if (lseek64 (pbh[i]->b_bdev->bd_dev, offset, SEEK_SET) != offset) {
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



#define STYPE( type ) printk( #type "\t%i\n", sizeof( type ) )

char *__prog_name;

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
				perror( "submit_bio: read failed\n" );
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

	trace_on( (rw == WRITE) ? TRACE_IO_W : TRACE_IO_R, "[%i] page io: %c, %llu, seek: %lli\n",
		  current->pid, ( rw == WRITE ) ? 'w' : 'r', bio -> bi_sector,
		  bio -> bi_sector - bio -> bi_bdev -> last_sector - 1 );
	bio -> bi_bdev -> last_sector = bio -> bi_sector;
	if( success )
		set_bit( BIO_UPTODATE, &bio -> bi_flags );
	else
		clear_bit( BIO_UPTODATE, &bio -> bi_flags );
	bio -> bi_size = 0;
	bio -> bi_end_io( bio, 0, 0 );
	return success ? 0 : -1;
}


/* fs/buffer.c */
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
	if (bh == 0) {
		return bh;
	}

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


znode *ulevel_allocate_znode( reiser4_tree *tree, znode *parent,
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
		dinfo( "%s[%i]: %s (%i), %Lx, %lx, %i\n", info -> prefix,
		       current->pid, name, namelen, offset,
		       ( long unsigned ) inum, ftype );
		return 0;
	} else {
		info -> fired = 0;
		return RETERR(-EINVAL);
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
	dinfo( "%s[%i]: %s (%i), %Lx, %lx, %i\n", info -> prefix,
	       current->pid, name, namelen, offset,
	       ( long unsigned ) inum, ftype );
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
	printk( "%i files inserted to create twig level\n", i );
	return 0;
}

static void call_umount (struct super_block * sb)
{
	struct file_system_type *fs = sb->s_type;

	fs->kill_sb(sb);

	fsync_bdev (sb->s_bdev);
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

	sprintf( dir_name, "Dir-%i", current->pid );
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_name.name = dir_name;
	dentry.d_name.len = strlen( dir_name );
	ret = info -> dir -> i_op -> mkdir( info -> dir,
					    &dentry, S_IFDIR | 0777 );
	dinfo( "In directory: %s", dir_name );

	if( ( ret != 0 ) && ( ret != -ENOMEM ) ) {
		reiser4_panic( "nikita-1636", "Cannot create dir: %i", ret );
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
		dinfo( "(%i) %i:%s %s/%s: %i\n", current->pid, i, op,
		       dir_name, name, ret );
		if( ( ret != 0 ) && ( ret != -EEXIST ) && ( ret != -ENOENT ) &&
		    ( ret != -EINTR ) && ( ret != -ENOMEM ) )
			reiser4_panic( "nikita-1493", "!!!" );

		if( info -> sleep && ( lc_rand_max( 10ull ) < 2 ) ) {
			delay.tv_sec  = 0;
			delay.tv_nsec = lc_rand_max( 1000000000ull );
			nanosleep( &delay, NULL );
		}
	}
	ul_init_file( &df );
	xmemset( &dentry, 0, sizeof dentry );

	call_readdir( f, dir_name );
	dinfo( "(%i): done.\n", current->pid );
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
	printk( "queue: %i %i\n", queue -> elements, queue -> capacity );
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
			printk( "(%i) %i: got: %i\n", current->pid, i, v );
			break;
		case producer:
			v = lc_rand_max( ( __u64 ) INT_MAX );
			mt_queue_put( queue, v );
			printk( "(%i) %i: put: %i\n", current->pid, i, v );
			break;
		default:
			impossible( "nikita-1917", "Revolution #9." );
			break;
		}
		mt_queue_info( queue );
	}
	printk( "(%i): done.\n", current->pid );
	REISER4_EXIT_PTR( NULL );
}

int nikita_test( int argc UNUSED_ARG, char **argv UNUSED_ARG,
		 reiser4_tree *tree )
{
	int ret;
	reiser4_key key;
	coord_t coord;
	int i;

	assert( "nikita-1096", tree != NULL );

	if( !strcmp( argv[ 2 ], "clean" ) ) {
		ret = cut_tree( tree, min_key(), max_key(), NULL );
		printf( "result: %i\n", ret );
	} else if( !strcmp( argv[ 2 ], "print" ) ) {
		print_tree_rec( "tree", tree, (unsigned) atoi( argv[ 3 ] ) );
	} else if( !strcmp( argv[ 2 ], "load" ) ) {
	} else if( !strcmp( argv[ 2 ], "unlink" ) ) {
		struct inode *f;
		char name[ 30 ];

		f = sandbox( get_root_dir( tree -> super ) );
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
		iput( f );
	} else if( !strcmp( argv[ 2 ], "dir" ) ||
		   !strcmp( argv[ 2 ], "rm" ) ||
		   !strcmp( argv[ 2 ], "mongo" ) ) {
		int threads;
		pthread_t *tid;
		mkdir_thread_info info;
		struct inode *f;

		f = sandbox( get_root_dir( tree -> super ) );
		create_twig( tree, f );
		threads = atoi( argv[ 3 ] );
		assert( "nikita-1494", threads > 0 );
		tid = xxmalloc( threads * sizeof tid[ 0 ] );
		assert( "nikita-1495", tid != NULL );

		print_inode( "inode", f );

		ret = call_create( f, "foo" );
		dinfo( "ret: %i\n", ret );
		ret = call_create( f, "bar" );
		dinfo( "ret: %i\n", ret );
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
		// print_tree_rec( "tree-dir", tree, REISER4_TREE_CHECK_ALL );
		iput( f );
	} else if( !strcmp( argv[ 2 ], "bobber" ) ) {
		struct inode *f;
		struct inode *bobber;
		struct inode *detritus;
		struct inode *site;
		char          buf[ 300 ];
		int           iterations;
		int           size;

		f = sandbox( get_root_dir( tree -> super ) );
		check_me( "nikita-2384", call_mkdir( f, "site" ) == 0 );
		check_me( "nikita-2379", call_mkdir( f, "bobber" ) == 0 );
		bobber = call_lookup( f, "bobber" );
		check_me( "nikita-2380", !IS_ERR( bobber ) );
		check_me( "nikita-2381", call_create( bobber, "detritus" ) == 0 );
		detritus = call_lookup( bobber, "detritus" );
		check_me( "nikita-2382", !IS_ERR( detritus ) );
		check_me( "nikita-2383",
			  call_write( detritus,
				      buf, (loff_t)0, sizeof buf ) == sizeof buf );

		iput( detritus );
		iput( bobber );

		site = call_lookup( f, "site" );
		check_me( "nikita-2385", !IS_ERR( site ) );

		iterations = atoi( argv[ 3 ] );
		size       = atoi( argv[ 4 ] );

		for( i = 0 ; i < iterations ; ++ i ) {
			struct inode *stuff;

			sprintf( buf, "stuff-%i", i );
			check_me( "nikita-2386", call_create( site, buf ) == 0 );
			stuff = call_lookup( site, buf );
			check_me( "nikita-2387", !IS_ERR( stuff ) );
			check_me( "nikita-2388",
				  call_write( stuff, buf, (loff_t)0, (unsigned) size ) == size );
			iput( stuff );
			print_percentage( ( ulong ) i,
					  ( ulong ) iterations, '+' );
		}
		iput( site );
		iput( f );
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
			reiser4_light_weight_stat lw;
			reiser4_unix_stat      un;
		} sd;
		struct inode *f;

		f = sandbox( get_root_dir( tree -> super ) );
		key_init( &key );
		set_key_locality( &key, ( __u64 ) f -> i_ino );
		set_key_type( &key, KEY_SD_MINOR );

		for( i = 0 ; ( i < atoi( argv[ 3 ] ) ) ||
			     ( tree -> height < TWIG_LEVEL ) ; ++ i ) {
			lock_handle lh;

			coord_init_zero( &coord );
			init_lh( &lh );

			printk( "_____________%i_____________\n", i );
			set_key_objectid( &key, ( __u64 ) 1000 + i * 8 );

			cputod16( 0x0 , &sd.base.extmask );
			cputod16( S_IFREG | 0111, &sd.lw.mode );
			cputod32( 1, &sd.lw.nlink );
			cputod64( 0x283746ull + i, &sd.lw.size );
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

			printk( "____end______%i_____________\n", i );

			done_lh( &lh );

		}
		// print_tree_rec( "tree:ibk", tree, REISER4_TREE_CHECK_ALL );
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
		f.i_atime.tv_sec = 0;
		f.i_mtime.tv_sec = 0;
		f.i_ctime.tv_sec = 0;
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
			reiser4_light_weight_stat lw;
			reiser4_unix_stat      un;
		} sd;

		for( i = 0 ; i < atoi( argv[ 3 ] ) ; ++ i ) {
			lock_handle lh;

			coord_init_zero( &coord );
			init_lh( &lh );

			printk( "_____________%i_____________\n", i );
			key_init( &key );
			set_key_objectid( &key, ( __u64 ) 1000 + i * 8 );

			cputod16( 0x0 , &sd.base.extmask );
			cputod16( S_IFREG | 0111, &sd.lw.mode );
			cputod32( 1, &sd.lw.nlink );
			cputod64( 0x283746ull + i, &sd.lw.size );
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

			printk( "____end______%i_____________\n", i );

			done_lh( &lh );
		}
		ret = cut_tree( tree, min_key(), max_key(), NULL );
		printf( "result: %i\n", ret );
		// print_tree_rec( "tree:cut", tree, REISER4_TREE_CHECK_ALL );
	} else if( !strcmp( argv[ 2 ], "sizeof" ) ) {
		STYPE( reiser4_key );
		STYPE( reiser4_tree );
		STYPE( cbk_cache_slot );
		STYPE( cbk_cache );
		STYPE( pos_in_item );
		STYPE( coord_t );
		STYPE( reiser4_item_data );
		STYPE( reiser4_inode );
		STYPE( reiser4_super_info_data );
		STYPE( plugin_header );
		STYPE( file_plugin );
		STYPE( tail_plugin );
		STYPE( hash_plugin );
		STYPE( perm_plugin );
		STYPE( reiser4_plugin );
		STYPE( inter_syscall_rap );
		STYPE( reiser4_plugin_ops );
		STYPE( item_header40 );
		STYPE( reiser4_block_nr );
		STYPE( znode );
		STYPE( d16 );
		STYPE( d32 );
		STYPE( d64 );
		STYPE( d64 );
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
			printk( "Usage: %s rounds arrays size\n", argv[ 0 ] );
		}
	} else {
		printk( "Huh?\n" );
	}
	return 0;
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


static struct inode *sandbox( struct inode * dir )
{
	char dir_name[ 100 ];
	struct inode *jail;

	sprintf( dir_name, "sandbox-%li", ( long ) getpid() );
	check_me( "nikita-1935", call_mkdir( dir, dir_name ) == 0 );
	jail = call_lookup( dir, dir_name );
	iput( dir );
	return jail;
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
	file.f_op = &reiser4_file_operations;
	readdir2 (prefix, &file, flags);
	file.f_op->release( dir, &file );
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



size_t BUFSIZE;
size_t MAX_BUFSIZE = 3000000;


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

	BUFSIZE = st->st_size;
	if (BUFSIZE > MAX_BUFSIZE)
		BUFSIZE = MAX_BUFSIZE;
	buf = xxmalloc (BUFSIZE);
	if (!buf) {
		perror ("copy_file: xxmalloc failed");
		iput (inode);
		return -ENOMEM;
	}

	count = BUFSIZE;
	off = 0;
	while (st->st_size) {
		int ret;

		if ((loff_t)count > st->st_size)
			count = st->st_size;
		if (read (fd, buf, count) != (ssize_t)count) {
			perror ("copy_file: read failed");
			iput (inode);
			return -errno;
		}
		ret = call_write (inode, buf, off, count);
		if (ret != (ssize_t)count) {
			info ("copy_file: write failed: %i: %i\n", ret, errno);
			iput (inode);
			return ret;
		}
/*
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
*/
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



static int bash_cp (char * real_file, struct inode * cwd, const char * name);
static int bash_cpr (struct inode * dir, const char * source)
{
	int result;
	DIR * d;
	struct dirent * dirent;
	struct stat st;
	struct inode * subdir;
	char * name;


	d = opendir (source);
	if (d == 0) {
		info ("opendir failed: %s\n", strerror (errno));
		return errno;
	}

	result = 0;
	while ((dirent = readdir (d)) != 0) {
		if (!strcmp (dirent->d_name, ".") ||
		    !strcmp (dirent->d_name, ".."))
			continue;
		asprintf (&name, "%s/%s", source, dirent->d_name);
		if (stat (name, &st) == -1) {
			result = errno;
			break;
		}
		info ("%s\n", name);
		if (S_ISDIR (st.st_mode)) {
			result = call_mkdir (dir, dirent->d_name);
			if (result)
				break;
			subdir = call_lookup (dir, dirent->d_name);
			if (IS_ERR (subdir)) {
				result = PTR_ERR (subdir);
				break;
			}
			result = bash_cpr (subdir, name);
			if (result) {
				closedir (d);
				return result;
			}
			iput (subdir);
			continue;
		}
		if (S_ISREG (st.st_mode)) {
			bash_cp (name, dir, dirent->d_name);
			continue;
		}
	}
	closedir (d);

	return result;
}


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
	/* read by 255 bytes */
	BUFSIZE = 255;

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


static int bash_diff_r (struct inode * dir, const char * source)
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
			subdir = call_lookup (dir, dirent->d_name);
			if (IS_ERR (subdir)) {
				result = PTR_ERR (subdir);
				break;
			}
			result = bash_diff_r (subdir, dirent->d_name);
			if (result) {
				closedir (d);
				return result;
			}
			continue;
		}
		if (S_ISREG (st.st_mode)) {
			bash_diff (dirent->d_name, dir, dirent->d_name);
			continue;
		}
	}
	closedir (d);
	chdir (cwd);
	free (cwd);
	return result;
}


/*
 * ls -lR
 */
static int get_one_name (void *arg, const char *name, int namelen UNUSED_ARG,
			 loff_t offset UNUSED_ARG, ino_t inum,
			 unsigned ftype UNUSED_ARG)
{
	echo_filldir_info *info;

	info = arg;
	info -> eof = 0;
	
	if( info -> fired == 0 ) {
		info -> fired = 1;
		info -> name = strdup( name );
		info -> inum = ( int ) inum;
		return 0;
	} else {
		info -> fired = 0;
		return -EINVAL;
	}
}

static int ls_lR (struct inode * inode, const char * path)
{
	struct dentry dentry;
	struct file file;
	echo_filldir_info info;
	int result;


	xmemset (&file, 0, sizeof (struct file));
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = inode;
	file.f_dentry = &dentry;
		
	xmemset( &info, 0, sizeof info );

	do {
		info.eof = 1;
		if (!inode->i_fop)
			/* this happens for symlinks */
			break;
		result = inode->i_fop->readdir (&file, &info, get_one_name);
		if( info.eof )
			break;
		if( info.name != NULL ) {
			struct inode *i;

			if( strcmp( info.name, "." ) &&
			    strcmp( info.name, ".." ) ) {
				/* skip "." and ".." */
				char * name;

				asprintf (&name, "%s/%s", path, info.name);

				i = call_lookup (inode, info.name);
				if( IS_ERR( i ) )
					warning( "nikita-2235", "Not found: %s",
						 info.name );
				else if( ( int ) i -> i_ino != info.inum )
					warning( "nikita-2236",
						 "Wrong inode number: %i != %i",
						 ( int ) info.inum, ( int ) i -> i_ino );
				else {
					info ("(%llu:%lu) %crw-r--r--\t%d\t%10llu\t%s\n",
					      reiser4_inode_data (i)->locality_id, i->i_ino,
					      S_ISREG (i->i_mode) ? '-' : 'd',
					      i->i_nlink, i->i_size, name);
					ls_lR (i, name);
				}
				free (name);
				free( info.name );
				iput( i );
			}			
		}
	} while( !info.eof && ( result == 0 ) );

	return result;
}

#include <regex.h>

static int rm_r (struct inode * dir, const char * path, regex_t * exp)
{
	struct dentry dentry;
	struct file file;
	echo_filldir_info info;
	int result;


	xmemset (&file, 0, sizeof (struct file));
	xmemset( &dentry, 0, sizeof dentry );
	dentry.d_inode = dir;
	file.f_dentry = &dentry;
		
	xmemset( &info, 0, sizeof info );

	do {
		info.eof = 1;
		result = dir->i_fop->readdir (&file, &info, get_one_name);
		if( info.eof )
			break;
		if( info.name != NULL ) {
			struct inode *i;

			if( strcmp( info.name, "." ) &&
			    strcmp( info.name, ".." ) ) {
				/* skip "." and ".." */
				char * name;

				asprintf (&name, "%s/%s", path, info.name);

				i = call_lookup (dir, info.name);
				if( IS_ERR( i ) )
					warning( "nikita-2304", "Not found: %s",
						 info.name );
				else if( ( int ) i -> i_ino != info.inum )
					warning( "nikita-2305",
						 "Wrong inode number: %i != %i",
						 ( int ) info.inum, ( int ) i -> i_ino );
				else {
					if (S_ISREG (i->i_mode)) {
						iput (i);
						if (regexec (exp, info.name, 0, 0, 0) == 0) {
							/* file name matches to patern */
							info ("%s\n", name);
							call_rm (dir, info.name);
						}
					} else {
						/* recursive to directory */
						rm_r (i, name, exp);
						iput (i);
					}
				}
				free (name);
				free( info.name );
			}			
		}
	} while( !info.eof && ( result == 0 ) );

	return result;
}


/* remove all regular files whose names match to @pattern */
static int bash_rmr (struct inode * dir, const char * pattern)
{
	regex_t exp;
	int result;

	result = regcomp (&exp, pattern, REG_NOSUB);
	if (result)
		return result;
	return rm_r (dir, ".", &exp);
}



static void *uswapd( void *untyped );

static int bash_mount (char * cmd, struct super_block **sb)
{
	char *opts;
	char *file_name;

	file_name = strsep( &cmd, (char *)" " );
	opts = cmd;

	*sb = call_mount (file_name, opts);
	if (IS_ERR (*sb)) {
		return PTR_ERR (*sb);
	}
#if 0
	if (getenv( "REISER4_SWAPD" )) {
		/* start uswapd */
		check_me ("vs-824", pthread_create( &uswapper, NULL, uswapd, *sb ) == 0);
	}
#endif

	return 0;
}


static void bash_umount (struct super_block * sb)
{
	int fd;

	if (getenv( "REISER4_SWAPD" )) {
		/* stop uswapd */
		going_down = 1;
		declare_memory_pressure();
		check_me( "vs-825", pthread_join( uswapper, NULL ) == 0 );
	}

	assert ("vs-768", sb);
	fd = sb->s_bdev->bd_dev;
	call_umount (sb);

	close (fd);

	/* free all pages and inodes, make sure that there are no dirty/used
	 * pages/inodes */
	invalidate_inodes (sb);
}


/* number of block on device */
static __u64 get_fs_size (struct super_block * s)
{
	struct stat st;
	loff_t size;

	check_me ("vs-749", fstat (s->s_bdev->bd_dev, &st) == 0);
	size = lseek64 (s->s_bdev->bd_dev, 0ull, SEEK_END);
	assert ("vs-750", size != (loff_t)-1);
	return size / s->s_blocksize;
}

#define TEST_MKFS_ROOT_LOCALITY   (41ull)
#define TEST_MKFS_ROOT_OBJECTID   (42ull)

/* this creates reiser4 filesystem of TEST_LAYOUT_ID */
static int bash_mkfs (char * file_name)
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
	char * p;
	reiser4_tail_id tail_id;
	reiser4_super_info_data *info;


	/* parse "mkfs options" */
	p = strchr (file_name, ' ');
	if (p == NULL) {
		info ("Using default tail policy: test\n");
		tail_id = TEST_TAIL_ID;
	} else {
		*p ++ = 0;
		if (!strcmp (p, "tail")) {
			tail_id = ALWAYS_TAIL_ID;
		} else if (!strcmp (p, "notail")) {
			tail_id = NEVER_TAIL_ID;
		} else if (!strcmp (p, "test")) {
			tail_id = TEST_TAIL_ID;
		} else if (!strcmp (p, "40")) {
			char * command;

			asprintf (&command, "echo y | ./reiser4progs/reiser4progs/mkfs/mkfs.reiser4 %s", file_name);
			if (system (command)) {
				free (command);
				info ("mkfs 40: %s\n", strerror (errno));
				return 1;
			}
			free (command);
			return 0;
		} else {
			info ("Usage: mkfs filename tail | notail | test | 40\n");
			return 1;
		}
	}

	info = kmalloc (sizeof (reiser4_super_info_data), GFP_KERNEL);
	if( info == NULL )
		BUG();
	super.s_fs_info = info;
	xmemset (info, 0, sizeof (reiser4_super_info_data));
	ON_DEBUG (INIT_LIST_HEAD (&info->all_jnodes));
	super.s_op = &reiser4_super_operations;
	super.s_root = &root_dentry;
	blocksize = getenv( "REISER4_BLOCK_SIZE" ) ?
		atoi( getenv( "REISER4_BLOCK_SIZE" ) ) : 512;
	super.s_blocksize = blocksize;
	for (super.s_blocksize_bits = 0; blocksize >>= 1; super.s_blocksize_bits ++);
	super.s_bdev = &bd;
	super.s_type = find_filesystem ("reiser4");

	super.s_bdev->bd_dev = open (file_name, O_RDWR);
	if (super.s_bdev->bd_dev == -1) {
		info ("Could not open device: %s\n", strerror (errno));
		return 1;
	}
	/* set number of blocks on device */
	reiser4_set_block_count (&super, get_fs_size (&super));

	xmemset( &root_dentry, 0, sizeof root_dentry );

	{

		REISER4_ENTRY( &super );
		txnmgr_init( &get_super_private (&super) -> tmgr );

		/* FIXME-ZAM: bash mkfs does not work if WRITE_LOG is ON, because
		 * it supplies empty overwrite set (this is unusual) to
		 * transaction manager, which triggers an error check code in
		 * the log writer. */

		get_super_private (&super)->df_plug = disk_format_plugin_by_id (TEST_FORMAT_ID);



		/*  make super block */
		{
			reiser4_master_sb * master_sb;
			size_t blocksize;
			__u64 block_count;


			blocksize = super.s_blocksize;
			bh = sb_bread (&super, (int)(REISER4_MAGIC_OFFSET / blocksize));
			assert ("vs-654", bh);
			memset (bh->b_data, 0, blocksize);

			/* master */
			master_sb = (reiser4_master_sb *)bh->b_data;
			strncpy (master_sb->magic, REISER4_SUPER_MAGIC_STRING, 4);
			cputod16 (TEST_FORMAT_ID, &master_sb->disk_plugin_id);
			cputod16 (blocksize, &master_sb->blocksize);


			/* block allocator */
			root_block = bh->b_blocknr + 1;
			next_block = root_block + 1;
			get_super_private (&super)->space_plug = space_allocator_plugin_by_id (TEST_SPACE_ALLOCATOR_ID);
			get_super_private (&super)->space_plug->
				init_allocator (get_space_allocator( &super ),
						&super, &next_block );

			/* oid allocator */
			get_super_private (&super)->oid_plug = oid_allocator_plugin_by_id (OID40_ALLOCATOR_ID);
			get_super_private (&super)->oid_plug->
				init_oid_allocator (get_oid_allocator (&super), 1ull, TEST_MKFS_ROOT_OBJECTID - 3);

			/* test layout super block */
			test_sb = (test_disk_super_block *)(bh->b_data + sizeof (*master_sb));
			strncpy (test_sb->magic, TEST_MAGIC, strlen (TEST_MAGIC));
			cputod16 (HASHED_DIR_PLUGIN_ID, &test_sb->root_dir_plugin);
			cputod16 (DEGENERATE_HASH_ID, &test_sb->root_hash_plugin);
			cputod16 (NODE40_ID, &test_sb->node_plugin);
			cputod16 (tail_id, &test_sb->tail_policy);


			/* block count on device */
			block_count = get_fs_size (&super);
			cputod64 (block_count, &test_sb->block_count);

			/* this will change on put_super in accordance to state
			 * of filesystem at that time */
			cputod64 (0ull, &test_sb->root_block);
			cputod16 (0, &test_sb->tree_height);
			cputod64 (0ull, &test_sb->next_free_block);
			cputod64 (TEST_MKFS_ROOT_OBJECTID - 3, &test_sb->next_free_oid);

			mark_buffer_dirty (bh);
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);

			/* initialize super block fields:
			 * number of blocks on device */
			reiser4_set_block_count(&super, block_count);
			/* number of used blocks */
			reiser4_set_data_blocks(&super, (__u64)(REISER4_MAGIC_OFFSET / blocksize) + 1);
			/* number of free blocks */
			reiser4_set_free_blocks(&super, block_count - ((REISER4_MAGIC_OFFSET / blocksize) + 1));

		}

		/* initialize empty tree */
		tree = &get_super_private( &super ) -> tree;
		init_formatted_fake( &super );
		tree -> super = &super;
		result = init_tree( tree, &root_block,
				    1/*tree_height*/, node_plugin_by_id( NODE40_ID ));
		tree -> cbk_cache.nr_slots = getenv( "REISER4_CBK_SLOTS" ) ?
			atoi( getenv( "REISER4_CBK_SLOTS" ) ) : CBK_CACHE_SLOTS;

		result = cbk_cache_init( &tree -> cbk_cache );
		fake = ulevel_allocate_znode( tree, NULL, 0, &FAKE_TREE_ADDR );
		root = ulevel_allocate_znode( tree, fake, tree->height, &tree->root_block);
		root -> rd_key = *max_key();
		sibling_list_insert( root, NULL );

		zput (root);
		zput (fake);

		{
			int result;
			struct inode * fake_parent, * inode;
			reiser4_key from;
			reiser4_key to;

			struct {
				reiser4_stat_data_base base;
				/* reiser4_light_weight_stat lw; */
			} sd;
			reiser4_item_data insert_data;
			reiser4_key key;

			/* key */
			key_init( &key );
			set_key_type( &key, KEY_SD_MINOR );
			set_key_locality( &key, 1ull );
			set_key_objectid( &key, TEST_MKFS_ROOT_LOCALITY - 10);

			/* item body */
			xmemset( &sd, 0, sizeof sd );
			cputod16( 0x0 , &sd.base.extmask );
			/* cputod16( S_IFDIR | 0111, &sd.lw.mode );
			cputod32( 1, &sd.lw.nlink );
			cputod64( 0ull, &sd.lw.size ); */

			/* data for insertion */
			insert_data.data = ( char * ) &sd;
			insert_data.user = 0;
			insert_data.length = sizeof (sd);
			insert_data.iplug = item_plugin_by_id (STATIC_STAT_DATA_ID);
			result = reiser4_grab_space_exact(100, 0);
			if (result) {
				info ("grabbing failed\n");
				return result;
			}


			result = insert_item (tree, &insert_data, &key);
			if (result) {
				info ("insert_item failed\n");
				return result;
			}
			
			/* get inode of fake parent */

			fake_parent = get_new_inode (&super, 2,
						     ul_find_actor,
						     ul_init_locked_inode,
						     &key);
			if (fake_parent == NULL)
				return -ENOMEM;
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
			all_grabbed2free();


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

			result = cut_tree (tree, &from, &to, NULL);
			if (result)
				return result;

			fake_parent->i_state &= ~I_DIRTY;
			inode->i_state &= ~I_DIRTY;
			iput (fake_parent);
			super.s_root->d_inode = inode;
			cputod64 (reiser4_inode_data( inode ) -> locality_id,
				  &test_sb->root_locality);
			cputod64 ((__u64)inode -> i_ino, &test_sb->root_objectid);
			cputod64 (tree -> root_block, &test_sb->root_block);
			cputod16 (tree -> height, &test_sb->tree_height);
			/* OIDS_RESERVED---macro defines in oid.c */
			cputod64 ( (__u64)( 1 << 16 ), &test_sb->next_free_oid);
			mark_buffer_dirty (bh);
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
			brelse (bh);

		}

		/*print_tree_rec ("mkfs", tree, REISER4_TREE_VERBOSE);*/

		result = __REISER4_EXIT( &__context );
		call_umount (&super);
		/*invalidate_pages ();*/
	}
	return result;
} /* bash_mkfs */



/* copy file into current directory */
static int bash_cp (char * real_file, struct inode * cwd, const char * name)
{
	struct stat st;
	int silent;
	int ret;
	
	if (stat (real_file, &st) || !S_ISREG (st.st_mode)) {
		errno ? perror ("stat failed") :
			info ("%s is not regular file\n", real_file);
		return 0;
	}
	silent = 1;
	ret = copy_file (real_file, cwd, name, &st, silent);
	if (ret) {
		info ("cp: copy_file failed: %i\n", ret);
	}
	return ret;
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
	info ("writing file %s from %d\nType data (# to end):\n", name, from);

	/* read stdin line by line and copy read data into buf. Break when '#'
	 * is encountered */
	buf = 0;
	n = 0;
	while (1) {
		char * tmp;
		int tmp_n;

		tmp = 0;
		tmp_n = 0;
		getline (&tmp, &tmp_n, stdin);
		if (tmp[0] == '#')
			break;
		buf = realloc (buf, (unsigned)(n + strlen (tmp)));
		assert ("vs-748", buf);
		memcpy (buf + n, tmp, strlen (tmp));
		n += strlen (tmp);
		free (tmp);
	}
	count = n;
	info ("Writing.. %d bytes\n", count);
	inode = call_lookup (dir, name);
	if (IS_ERR (inode)) {
		info ("write: lookup failed\n");
		return 0;
	}
	if (call_write (inode, buf, (loff_t)from, (unsigned)count) != (ssize_t)count) {
		info ("write failed\n");
		return 0;
	}
	info ("done\n");
	iput (inode);
	free (buf);
	return 0;
}

static void bash_df (struct inode * cwd)
{
	struct statfs st;

	cwd -> i_sb -> s_op -> statfs( cwd -> i_sb, &st );
	printk( "\n\tf_type: %lx", st.f_type );
	printk( "\n\tf_bsize: %li", st.f_bsize );
	printk( "\n\tf_blocks: %li", st.f_blocks );
	printk( "\n\tf_bfree: %li", st.f_bfree );
	printk( "\n\tf_bavail: %li", st.f_bavail );
	printk( "\n\tf_files: %li", st.f_files );
	printk( "\n\tf_ffree: %li", st.f_ffree );
	printk( "\n\tf_fsid: %lx", st.f_fsid );
	printk( "\n\tf_namelen: %li\n", st.f_namelen );
}

static int bash_trace (struct inode * cwd, const char * cmd)
{
	__u32 flags;

	if( sscanf( cmd, "%i", &flags ) != 1 ) {
		printk( "usage: trace N\n" );
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


struct cpr_thread_info {
	struct inode * dir; /* inode of current directory where cp-r is
			     * started */
	char * source; /* name of directory to copy */
	int num;
};


void * cpr_thread (struct cpr_thread_info * info)
{
	char name [10];
	struct inode * dir;
	char * source_path;


	/* create a directory where thread wll work */
	sprintf (name, "%d", info->num);
	if (call_mkdir (info->dir, name)) {
		info ("Could not create directory \"%s\"\n", name);
		return 0;
	}
	dir = call_lookup (info->dir, name);
	if (IS_ERR (dir)) {
		info ("lookup failed");
		return 0;
	}
	info ("cpr [%i]\n", current->pid);

	asprintf (&source_path, "%s", info->source);
	bash_cpr (dir, source_path);
	iput (dir);
	return 0;
}

void * cpr_thread_start (void *arg)
{
	REISER4_ENTRY_PTR (((struct cpr_thread_info *)arg)->dir->i_sb);
	cpr_thread (arg);
	REISER4_EXIT_PTR (NULL);
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
              "\tcp-r dir       - copy directory recursively\n"\
	      "\trm-r pattern   - remove all files matching to regular expression\n"\
	      "\tdiff filename  - compare files\n"\
              "\tdiff-r dir     - compare directories recursively\n"\
	      "\ttrunc filename size - truncate file\n"\
	      "\ttouch          - create empty file\n"\
	      "\tread filename from count\n"\
	      "\twrite filename from\n"\
	      "\trm             - remove file\n"\
	      "\talloc          - allocate unallocated extents\n"\
	      "\tsqueeze        - squeeze twig level\n"\
	      "\ttail [on|off]  - set or get state of tail plugin of current directory\n"\
	      "\tp              - print tree\n"\
	      "\tinfo           - print fs info (height, root, etc)\n"\
              "\tstat           - print reiser4 stats\n"\
	      "\texit\n");



extern void run_init_reiser4( void );
extern void run_done_reiser4( void );

static int bash_test (int argc UNUSED_ARG, char **argv UNUSED_ARG,
		      reiser4_tree *tree UNUSED_ARG)
{
	char * command = 0;
	struct inode * cwd;
	/*reiser4_context context;*/
	int mounted;
	int result;
	char * tmp;
	int tmp_n;
	struct super_block * sb;


	mounted = 0;
	sb = 0;

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

	tmp = 0;
	tmp_n = 0;
	while (1) {
		info ("> ");
		if (getline (&tmp, &tmp_n, stdin) == -1)
			break;
		if (tmp[0] == '#')
			/* ignore comments */
			continue;
		/*add_history (command);*/
		/* remove \n */
		tmp [strlen (tmp) - 1] = 0;
		command = tmp;
		if (!strncmp (command, "mount ", 6)) {
			if (mounted) {
				info ("Umount first\n");
				continue;
			}
			result = bash_mount (command + 6, &sb);
			if (result) {
				info ("mount failed: %s\n", strerror (-result));
				continue;
			}
			assert ("vs-767", sb);
			cwd = sb->s_root->d_inode;
			mounted = 1;

			continue;
		}
		if (!strcmp (command, "umount")) {
			if (!mounted) {
				info ("Mount first\n");
				continue;
			}
			bash_umount (sb);
			mounted = 0;
			sb = 0;
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
				bash_umount (sb/*&context*/);
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

			tmp = call_lookup (cwd, command + 3);
			if (IS_ERR(tmp)) {
				info ("%s failed\n", command);
			} else if (!S_ISDIR (tmp->i_mode)) {
				info ("%s is not a directory\n", command + 3);
				iput (tmp);
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
		BASH_CMD ("rm-r ", bash_rmr);
		BASH_CMD ("diff-r ", bash_diff_r);
		BASH_CMD ("trace ", bash_trace);

		BASH_CMD3 ("cp ", bash_cp);
		BASH_CMD3 ("diff ", bash_diff);
		if (!strcmp ("lR", command)) {
			ls_lR (cwd, ".");
			continue;
		}
#define CPR_THREADS 3
		if (!strncmp (command, "mcpr", 4)) {
			pthread_t tid [CPR_THREADS];
			int i;
			struct cpr_thread_info info [CPR_THREADS];

			for (i = 0; i < CPR_THREADS; ++ i) {
				info[i].source = command + 5;
				info[i].dir = cwd;
				info[i].num = i;
				pthread_create (&tid [i], NULL,
						cpr_thread_start, &info [i]);
			}
			for (i = 0 ; i < CPR_THREADS; ++ i)
				pthread_join (tid [i], NULL);

		} else if (!strncmp (command, "tail", 4)) {
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
		} else if (!strcmp (command, "df")) {
			bash_df (cwd);
		} else if (!strncmp (command, "p", 1)) {
			REISER4_ENTRY (sb);
			/*
			 * print tree
			 */
			if (!strncmp (command, "pp", 2)) {
				/* iterate */
				print_tree_rec ("DONE", tree_by_inode (cwd),
						REISER4_COLLECT_STAT);
			} else if (!strncmp (command, "pb", 2)) {
				print_tree_rec ("BRIEF", tree_by_inode (cwd),
						REISER4_NODE_PRINT_HEADER);
			} else {
				print_tree_rec ("DONE", tree_by_inode (cwd),
						REISER4_TREE_VERBOSE);
			}
			__REISER4_EXIT (&__context);
		} else if (!strncmp (command, "info", 1)) {
			REISER4_ENTRY (sb);
			get_current_super_private ()->df_plug->print_info (reiser4_get_current_sb ());
			__REISER4_EXIT (&__context);
		} else if (!strncmp (command, "stat", 4)) {
			REISER4_ENTRY (sb);
			reiser4_print_stats();
			__REISER4_EXIT (&__context);
		} else
			print_help ();
	}
	free (tmp);
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


/*****************************************************************************************
 *                                      BITMAP TEST
 *****************************************************************************************/

#define BLOCK_COUNT 14000

#if 0
static int shrink_cache (void);

static void *uswapd( void *untyped )
{
	struct super_block *super = untyped;
	REISER4_ENTRY_PTR( super );

	current->i_am_swapd = 1;
	while( 1 ) {
		int flushed  = 0;

		/*
		 * wait for next memory pressure request
		 */
		spin_lock( &mp_guard );
		while( !is_mp && !going_down )
			kcond_wait( &memory_pressed, &mp_guard, 0 );
		is_mp = 0;
		spin_unlock( &mp_guard );
		if( going_down )
			break;
		reiser4_log( "nikita-1939", "uswapd wakes up..." );

		flushed = shrink_cache ();
		trace_on (TRACE_PCACHE, "shrink_cache released %d pages\n",
			  flushed);

		/* wakeup xxmalloc */
		spin_lock( &mp_guard );
		is_mp_done = 1;
		spin_unlock( &mp_guard );
		kcond_broadcast( &memory_pressure_done );
		
	}
	REISER4_EXIT_PTR( NULL );
}

static int try_to_release_page (struct page * page, int gfp_mask)
{
	return page->mapping->a_ops->releasepage (page, gfp_mask);
}

/* scan list of pages, writeback dirty pages, release freeable pages. Return
 * number of freed pages */
static int shrink_cache (void)
{
	struct page * page;
	int removed;
	int i;

	removed = 0;

	spin_lock (&page_list_guard);

	for (i = 0; i < (int)nr_pages; ++ i) {
		struct list_head * tmp;

		tmp = page_lru_list.prev;
		page = list_entry (tmp, struct page, lru);
		list_del_init(tmp);
		list_add(tmp, &page_lru_list);

		if (!PagePrivate (page) || !page->mapping) {
			assert ("vs-820", page_count (page) && page_count (page) < 3);
			continue;
		}

		if (PageWriteback (page)) {
			continue;
		}
		if (!spin_trylock (&page->lock))
			/* page is locked already */
			continue;
		check_me ("vs-826", !TestSetPageLocked (page));
		if (PageWriteback (page)) {
			unlock_page (page);
			continue;
		}
		if (PageDirty (page) && page_count (page) == 2) {
			int nr_pages = 32;

			page_cache_get (page);
			spin_unlock (&page_list_guard);
			page->mapping->a_ops->vm_writeback (page, &nr_pages);
			page_cache_release (page);
			spin_lock (&page_list_guard);
			continue;
		}
		assert ("vs-823", PagePrivate(page));

		spin_unlock (&page_list_guard);
		
		/* avoid to free a locked page */
		page_cache_get(page);
		
		if (!try_to_release_page (page, 0)) {
			/* releasepage failed: page's jnode is in
			 * transaction */
			unlock_page (page);
			page_cache_release (page);

			spin_lock (&page_list_guard);
			continue;
		}

		page_cache_release(page);
				
		spin_lock (&page_list_guard);
			
		{
			struct address_space *mapping;

			mapping = page->mapping;
			write_lock (&mapping->page_lock);
			remove_inode_page (page);
			write_unlock (&mapping->page_lock);
		}

		/* free page */
		assert ("vs-822", page_count (page) == 1);
		list_del_init (&page->lru);
		nr_pages --;

		trace_on (TRACE_PCACHE, "page freed: page: %p (index %lu, ino %lu)\n",
			  page, page->index, page->mapping->host->i_ino);
		unlock_page(page);
		page_cache_release(page);
		removed ++;
	}
	spin_unlock (&page_list_guard);
	return removed;
}

#endif

void declare_memory_pressure( void )
{
	spin_lock( &mp_guard );
	is_mp = 1;
	spin_unlock( &mp_guard );
	kcond_broadcast( &memory_pressed );
	dinfo( "Memory pressure declared: %lli", total_allocations );
	/*total_allocations = 0;*/
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
unsigned long PAGE_CACHE_SIZE;
unsigned long PAGE_CACHE_MASK;

int real_main( int argc, char **argv )
{
	int result;
	struct super_block *s;
	reiser4_tree *tree;
	reiser4_context __context;
	int blocksize;
	char *e;

	mallopt( M_MMAP_MAX, 0 );
	mallopt( M_TRIM_THRESHOLD, INT_MAX );

	dinfo("node size: %d\n", sizeof(node40_header));
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
	e = getenv( "REISER4_BLOCK_SIZE" );
	blocksize = e ? atoi( e ) : 512;
	for (PAGE_CACHE_SHIFT = 0; blocksize >>= 1; PAGE_CACHE_SHIFT ++);
	
	PAGE_CACHE_SIZE	= (1UL << PAGE_CACHE_SHIFT);
	PAGE_CACHE_MASK	= (~(PAGE_CACHE_SIZE-1));
	dinfo ("PAGE_CACHE_SHIFT=%d, PAGE_CACHE_SIZE=%lu, PAGE_CACHE_MASK=0x%lx\n",
	      PAGE_CACHE_SHIFT, PAGE_CACHE_SIZE, PAGE_CACHE_MASK);

	if( getenv( "REISER4_TRAP" ) ) {
		trap_signal( SIGBUS );
		trap_signal( SIGSEGV );
	}

	e = getenv( "REISER4_TRACE_FLAGS" );
	if( e != NULL ) {
		reiser4_current_trace_flags =
			strtol( e, NULL, 0 );
	}

	e = getenv( "REISER4_KMALLOC_FAILURE_RATE" );
	if( KMEM_FAILURES && ( e != NULL ) )
		kmalloc_failure_rate = strtol( e, NULL, 0 );
	else
		kmalloc_failure_rate = 0;

	INIT_LIST_HEAD( &inode_hash_list );
	INIT_LIST_HEAD( &page_lru_list );
	nr_pages = 0;
	/*pc_hash_init( &page_htable, PAGE_HASH_TABLE_SIZE );*/

	/*
	 *
	 */
	spin_lock_init( &mp_guard );
	kcond_init( &memory_pressed );
	kcond_init( &memory_pressure_done );
	going_down = 0;

	/*
	 * FIXME-VS: will be fixed
	 */
	if (argc == 2 && !strcmp (argv[1], "sh")) {
		/*
		 * it starts uswapd on mount
		 */
		bash_test (argc, argv, 0);
	}

	set_current ();

	e = getenv( "REISER4_MOUNT" );
	if( e == NULL ) {
		warning( "nikita-2175", "Set REISER4_MOUNT" );
		return 0;
	}
	run_init_reiser4 ();
	s = call_mount( e, getenv( "REISER4_MOUNT_OPTS" ) ? : "" );
	if( IS_ERR( s ) ) {
		warning( "nikita-2175", "Cannot mount: %li", PTR_ERR( s ) );
		return PTR_ERR( s );
	}
	s = &super_blocks[0];
	tree = &get_super_private(s) -> tree;

	init_context( &__context, s );

	assert ("jmacd-998", s -> s_blocksize == (unsigned)PAGE_CACHE_SIZE /* don't blame me, otherwise. */);
	

#if 0
	result = pthread_create( &uswapper, NULL, uswapd, s );
	assert( "nikita-1938", result == 0 );
#endif

	/* check that blocksize is a power of two */
	assert( "vs-417", ! ( s -> s_blocksize & ( s -> s_blocksize - 1 ) ) );
	
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

	printk( "tree height: %i\n", tree -> height );

	/*
	 * shut down uswpad
	 */
	going_down = 1;
	declare_memory_pressure();
	check_me( "nikita-2254", pthread_join( uswapper, NULL ) == 0 );

	check_me( "nikita-2514", txn_end( &__context ) == 0 );
	done_context( &__context );
	/*sb = reiser4_get_current_sb ();*/
	{
		int fd;

		fd = s->s_bdev->bd_dev;
		call_umount (s);
		
		close (fd);
		
		/* free all pages and inodes, make sure that there are no dirty/used
		 * pages/inodes */
		invalidate_inodes (s);
		/*invalidate_pages ();*/

		/*bash_umount ( &__context );*/
	}
	run_done_reiser4 ();
	return 0;
}

int main (int argc, char **argv)
{
	int ret;
	
	if ((ret = pthread_key_create (& __current_key, free_current)) != 0) {
		/* okay, but reiser4_panic seg faults if current == NULL :( */
		reiser4_panic("jmacd-901", "pthread_key_create failed");
	}

	return real_main (argc, argv);
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
		printk( "Cannot load/parse node: %i", ret );
		return;
	}

	sprintf_key( buffer_l, &node -> ld_key );
	sprintf_key( buffer_r, &node -> rd_key );

	fprintf( dot, "B%lli [shape=record,label=\"%lli\\n%s\\n%s\"];\n",
		 *znode_get_block( node ), *znode_get_block( node ),
		 buffer_l, buffer_r );

	for( coord_init_before_first_item( &coord, node ); coord_next_item( &coord ) == 0; ) {

		if( item_is_internal( &coord ) ) {
			znode *child;

			child = UNDER_SPIN( dk, znode_get_tree( coord.node ),
					    child_znode( &coord,
							 coord.node, 0, 0 ) );
			if( !IS_ERR( child ) ) {
				tree_rec_dot( tree, child, flags, dot );
				fprintf( dot, "B%lli -> B%lli ;\n",
					 *znode_get_block( node ),
					 *znode_get_block( child ) );
				zput( child );
			} else {
				printk( "Cannot get child: %li\n",
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
			write_lock( &mapping->page_lock );
			list_del_init(&page->list);
			list_add(&page->list, &mapping->dirty_pages);
			write_unlock( &mapping->page_lock );
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

	assert ("nikita-2176", PageLocked(page));

	if (wait && PageWriteback(page))
		wait_on_page_writeback(page);

	spin_lock(&mapping->page_lock);
	list_del_init(&page->list);
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

void end_page_writeback(struct page *page)
{
	if (!TestClearPageWriteback(page))
		BUG();
}

int block_sync_page(struct page *page UNUSED_ARG)
{
	return 0;
}

void blk_run_queues (void) { }

int inode_setattr( struct inode * inode, struct iattr * attr )
{
	unsigned int ia_valid = attr->ia_valid;
	int error = 0;
	
	lock_kernel();
	if (ia_valid & ATTR_SIZE) {
		/*
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			goto out;
		*/
	}

	if (ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		inode->i_mtime = attr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		inode->i_mode = attr->ia_mode;
	}
	mark_inode_dirty(inode);
	unlock_kernel();
	return error;
}

int inode_change_ok( struct inode *inode UNUSED_ARG,
		     struct iattr *attr UNUSED_ARG )
{
	return 0;
}

loff_t default_llseek( struct file *file, loff_t offset, int origin )
{
	long long retval;

	lock_kernel();
	switch (origin) {
		case 2:
			offset += file->f_dentry->d_inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = ++event;
		}
		retval = offset;
	}
	unlock_kernel();
	return retval;
}

int seq_printf(struct seq_file *file UNUSED_ARG, const char *f UNUSED_ARG, ...)
{
	return 0;
}

int vfs_readlink(struct dentry *dentry UNUSED_ARG,
		 char *buffer UNUSED_ARG,
		 int buflen UNUSED_ARG, const char *link UNUSED_ARG)
{
	return 0;
}

int vfs_follow_link(struct nameidata *nd UNUSED_ARG,
		    const char *link UNUSED_ARG)
{
	return 0;
}

void balance_dirty_pages(struct address_space *mapping UNUSED_ARG)
{
}

void add_timer(struct timer_list * timer UNUSED_ARG)
{
}

int del_timer(struct timer_list * timer UNUSED_ARG)
{
	return 0;
}

typedef struct {
	int (*fn)(void *);
	void * arg;
} kernel_thread_args;

static void *kernel_thread_helper( void *dummy )
{
	kernel_thread_args *arg;

	arg = dummy;
	set_current();
	return ( void * ) arg -> fn( arg -> arg );
}

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags UNUSED_ARG)
{
	pthread_t id;
	kernel_thread_args *args;
	pthread_attr_t kattr;

	check_me( "nikita-2458", args = xxmalloc( sizeof *args ) );
	args -> fn  = fn;
	args -> arg = arg;
	pthread_attr_init( &kattr );
	pthread_attr_setdetachstate( &kattr, PTHREAD_CREATE_DETACHED );
	check_me( "nikita-2457",
		  pthread_create( &id, &kattr,
				  kernel_thread_helper, args ) == 0 );
	pthread_attr_destroy( &kattr );
	return id;
}

struct file *filp_open(const char * filename, int flags, int mode)
{
	int fd;
	struct file *result;

	fd = open( filename, flags, mode );
	if( fd == -1 )
		return ERR_PTR( -errno );

	result = kmalloc( sizeof *result, GFP_KERNEL );
	check_me( "nikita-2508", result == 0 );
	xmemset( result, 0, sizeof *result );
	result -> f_ufd = fd;
	return result;
}

int filp_close(struct file *filp, fl_owner_t id UNUSED_ARG)
{
	return close( filp -> f_ufd );
}

unsigned int nr_free_pagecache_pages( void )
{
	return ( unsigned int ) ~0;
}

unsigned int nr_free_pages( void )
{
	return ( unsigned int ) ~0;
}

void init_completion(struct completion *x)
{
	init_MUTEX_LOCKED( &x -> sem );
}

void wait_for_completion(struct completion *x)
{
	down( &x -> sem );
}

void complete(struct completion *x)
{
	up( &x -> sem );
}

void complete_and_exit( struct completion *comp, long code UNUSED_ARG )
{
	if( comp )
		complete( comp );
	for(;0;)
		;
}

/**
 * clear_inode - clear an inode
 * @inode: inode to clear
 *
 * This is called by the filesystem to tell us
 * that the inode is no longer useful. We just
 * terminate it with extreme prejudice.
 */

void clear_inode(struct inode *inode)
{
	// invalidate_inode_buffers(inode);

	if (inode->i_data.nrpages)
		BUG();
	if (inode->i_state & I_CLEAR)
		BUG();
	// wait_on_inode(inode);
	DQUOT_DROP(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->clear_inode)
		inode->i_sb->s_op->clear_inode(inode);
	inode->i_state = I_CLEAR;
}

void fsync_super( struct super_block *s )
{
	fsync_bdev (s->s_bdev);
}

/*
 * Clear a page's dirty flag, while caring for dirty memory accounting.
 * Returns true if the page was previously dirty.
 */
int test_clear_page_dirty(struct page *page)
{
	if (TestClearPageDirty(page)) {
		return 1;
	}
	return 0;
}

struct page * filemap_nopage(struct vm_area_struct * area,
			     unsigned long address, int unused)
{
	return NULL;
}

/*
 * inode_lock must be held
 */
void __iget(struct inode * inode)
{
	atomic_inc(&inode->i_count);
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

