/*
 * Copyright 2001 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation stuff.
 */

#if !defined( __REISER4_ULEVEL_H__ )
#define __REISER4_ULEVEL_H__

#define NOT_YET                        (0)
#define YOU_CAN_COMPILE_PSEUDO_CODE    (0)
#define BROKEN_WITHOUT_SLUMS           (0)

#include <errno.h>
#include <asm/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

/* for rdtscll() */
#include <asm/msr.h>

/* for __le??_to_cpu() */
#include <linux/byteorder/little_endian.h>

#include "atomic.h"

/* This let's us test with posix locks, thereby allowing use of typedef'd spinlock_t as
 * the lock guarding our condition variables. */
#define KUT_LOCK_POSIX 1
/*#define KUT_LOCK_SPINLOCK 1*/
/*#define KUT_LOCK_ERRORCHECK 1*/
#define SPINLOCK_BUG(x) spinlock_bug (x);

extern void spinlock_bug (const char *msg);

#include "kutlock.h"

#include "reiser4.h"
#include "debug.h"
#include "reiser4_i.h"
#include "reiser4_sb.h"

#define HAS_BFD                         (1)
#define USE_DLADDR_DLSYM_FOR_BACKTRACE  (1)
#define ULEVEL_DROP_CORE                (0)

#define no_context (0)
#define current_pname   (__prog_name) /* __libc_argv[ 0 ] */
#define current_pid     ( ( int ) pthread_self() )
#define UNUSE __attribute__( ( unused ) )
#define GFP_KERNEL     (0)
#define printk printf
#define SLAB_HWCACHE_ALIGN (0)
#define CURRENT_TIME ( ( __u32 ) time( NULL ) )
#define likely( x ) ( x )
#define unlikely( x ) ( x )
#define BUG() do { printf ("BUG! help!\n"); abort (); } while (0)

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

/** from <linux/kernel.h>
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	const typeof(x) _x = (x);	\
	const typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

/* from <linux/list.h> */

/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new entry between two known consecutive entries. 
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __inline__ void __list_add(struct list_head * new,
	struct list_head * prev,
	struct list_head * next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __inline__ void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __inline__ void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __inline__ void __list_del(struct list_head * prev,
				  struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is in an undefined state.
 */
static __inline__ void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static __inline__ void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry); 
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static __inline__ int list_empty(struct list_head *head)
{
	return head->next == head;
}

/**
 * list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static __inline__ void list_splice(struct list_head *list, struct list_head *head)
{
	struct list_head *first = list->next;

	if (first != list) {
		struct list_head *last = list->prev;
		struct list_head *at = head->next;

		first->prev = head;
		head->next = first;

		last->next = at;
		at->prev = last;
	}
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
        	pos = pos->next, prefetch(pos->next))
        	
/**
 * list_for_each_safe	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop counter.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)


/* end of <linux/list.h> */

static inline void prefetch(const void *x UNUSED_ARG ) {;}

extern pthread_key_t __current_key;

extern __u32 set_current ();

#define current ((struct task_struct*) pthread_getspecific (__current_key))
	

typedef struct semaphore {
	pthread_mutex_t  mutex;
} semaphore;

typedef struct kmem_cache_t {
	size_t      size;
	const char* name;
	unsigned    count;
	spinlock_t  lock;
} kmem_cache_t;

struct qstr {
	const unsigned char * name;
	unsigned int len;
	unsigned int hash;
};

#define DNAME_INLINE_LEN 16

struct dentry {
	atomic_t d_count;
	unsigned int d_flags;
	struct inode  * d_inode;	/* Where the name belongs to - NULL is negative */
	struct dentry * d_parent;	/* parent directory */
	struct list_head d_hash;	/* lookup hash list */
	struct list_head d_lru;		/* d_count = 0 LRU list */
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children */
	struct list_head d_alias;	/* inode alias list */
	int d_mounted;
	struct qstr d_name;
	unsigned long d_time;		/* used by d_revalidate */
	struct dentry_operations  *d_op;
	struct super_block * d_sb;	/* The root of the dentry tree */
	unsigned long d_vfs_flags;
	void * d_fsdata;		/* fs-specific data */
	unsigned char d_iname[DNAME_INLINE_LEN]; /* small names */
};

struct fown_struct {
	int pid;		/* pid or -pgrp where SIGIO should be sent */
	uid_t uid, euid;	/* uid/euid of process setting the owner */
	int signum;		/* posix.1b rt signal to be delivered on IO */
};


struct file {
	struct list_head	f_list;
	struct dentry		*f_dentry;
	struct vfsmount         *f_vfsmnt;
	struct file_operations	*f_op;
	atomic_t		f_count;
	unsigned int 		f_flags;
	mode_t			f_mode;
	loff_t			f_pos;
	unsigned long 		f_reada, f_ramax, f_raend, f_ralen, f_rawin;
	struct fown_struct	f_owner;
	unsigned int		f_uid, f_gid;
	int			f_error;

	unsigned long		f_version;

	/* needed for tty driver, and maybe others */
	void			*private_data;

	/* preallocated helper kiobuf to speedup O_DIRECT */
	struct kiobuf		*f_iobuf;
	long			f_iobuf_lock;
};

typedef int (*filldir_t)(void *, const char *, int, loff_t, ino_t, unsigned);
struct poll_table_struct;
struct vm_area_struct;
struct file_lock;
struct iovec;
struct page;
struct nameidata;

struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
	int (*readdir) (struct file *, void *, filldir_t);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
};

struct iattr;

struct inode_operations {
	int (*create) (struct inode *,struct dentry *,int);
	struct dentry * (*lookup) (struct inode *,struct dentry *);
	int (*link) (struct dentry *,struct inode *,struct dentry *);
	int (*unlink) (struct inode *,struct dentry *);
	int (*symlink) (struct inode *,struct dentry *,const char *);
	int (*mkdir) (struct inode *,struct dentry *,int);
	int (*rmdir) (struct inode *,struct dentry *);
	int (*mknod) (struct inode *,struct dentry *,int,int);
	int (*rename) (struct inode *, struct dentry *,
			struct inode *, struct dentry *);
	int (*readlink) (struct dentry *, char *,int);
	int (*follow_link) (struct dentry *, struct nameidata *);
	void (*truncate) (struct inode *);
	int (*permission) (struct inode *, int);
	int (*revalidate) (struct dentry *);
	int (*setattr) (struct dentry *, struct iattr *);
	int (*getattr) (struct dentry *, struct iattr *);
};

struct statfs {
	long f_type;
	long f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	long f_fsid;
	long f_namelen;
	long f_spare[6];
};

struct seq_file;
struct super_operations {
	void (*read_inode) (struct inode *);
    	void (*read_inode2) (struct inode *, void *) ;
   	void (*dirty_inode) (struct inode *);
	void (*write_inode) (struct inode *, int);
	void (*put_inode) (struct inode *);
	void (*delete_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	void (*write_super) (struct super_block *);
	void (*write_super_lockfs) (struct super_block *);
	void (*unlockfs) (struct super_block *);
	int (*statfs) (struct super_block *, struct statfs *);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*clear_inode) (struct inode *);
	void (*umount_begin) (struct super_block *);
	struct dentry * (*fh_to_dentry)(struct super_block *sb, __u32 *fh, int len, int fhtype, int parent);
	int (*dentry_to_fh)(struct dentry *, __u32 *fh, int *lenp, int need_parent);
	int (*show_options)(struct seq_file *, struct vfsmount *);
};

struct task_struct {
	char *comm;
	int   pid;
	void *journal_info;
	__u32         fsuid;
	__u32         fsgid;
};

struct block_device {
	void * vp;
};

typedef unsigned short kdev_t;

struct super_block {
	kdev_t			s_dev;
	struct block_device   * s_bdev;
	unsigned long s_blocksize;
	unsigned char s_blocksize_bits;
	struct dentry *s_root;
	void *s_op;
	union {
		struct reiser4_sb_info      reiser4_sb;
	} u;
};

struct address_space;
struct kiobuf;

struct address_space_operations {
	int (*writepage)(struct page *);
	int (*readpage)(struct file *, struct page *);
	int (*sync_page)(struct page *);
	/*
	 * ext3 requires that a successful prepare_write() call be followed
	 * by a commit_write() call - they must be balanced
	 */
	int (*prepare_write)(struct file *, struct page *, unsigned, unsigned);
	int (*commit_write)(struct file *, struct page *, unsigned, unsigned);
	/* Unfortunately this kludge is needed for FIBMAP. Don't use it */
	int (*bmap)(struct address_space *, long);
	int (*flushpage) (struct page *, unsigned long);
	int (*releasepage) (struct page *, int);
#define KERNEL_HAS_O_DIRECT /* this is for modules out of the kernel */
	int (*direct_IO)(int, struct inode *, struct kiobuf *, unsigned long, int);
};

struct address_space {
	struct list_head	clean_pages;	/* list of clean pages */
	struct list_head	dirty_pages;	/* list of dirty pages */
	struct list_head	locked_pages;	/* list of locked pages */
	unsigned long		nrpages;	/* number of total pages */
	struct address_space_operations *a_ops;	/* methods */
	struct inode		*host;		/* owner: inode, block_device */
	struct vm_area_struct	*i_mmap;	/* list of private mappings */
	struct vm_area_struct	*i_mmap_shared; /* list of shared mappings */
	spinlock_t		i_shared_lock;  /* and spinlock protecting it */
	int			gfp_mask;	/* how to allocate the pages */
};

struct inode {
	struct list_head	i_hash;
	struct list_head	i_list;
	struct list_head	i_dentry;
	
	struct list_head	i_dirty_buffers;
	struct list_head	i_dirty_data_buffers;

	unsigned long		i_ino;
	atomic_t		i_count;
	__u16			i_dev;
	umode_t			i_mode;
	nlink_t			i_nlink;
	uid_t			i_uid;
	gid_t			i_gid;
	__u16			i_rdev;
	loff_t			i_size;
	time_t			i_atime;
	time_t			i_mtime;
	time_t			i_ctime;
	unsigned int		i_blkbits;
	unsigned long		i_blksize;
	unsigned long		i_blocks;
	unsigned long		i_version;
	struct semaphore	i_sem;
	struct semaphore	i_zombie;
	struct inode_operations	*i_op;
	struct file_operations	*i_fop;	/* former ->i_op->default_file_ops */
	struct super_block	*i_sb;
	struct address_space	*i_mapping;
	struct address_space	i_data;
	struct list_head	i_devices;
	unsigned long		i_dnotify_mask; /* Directory notify events */
	unsigned long		i_state;

	unsigned int		i_flags;
	unsigned char		i_sock;

	atomic_t		i_writecount;
	unsigned int		i_attr_flags;
	__u32			i_generation;
	union {
		struct reiser4_inode_info	reiser4_i;
		void				*generic_ip;
	} u;
};

struct root_dir {
	struct inode *d_inode;
};

extern unsigned long get_jiffies ();

#define jiffies get_jiffies()

extern void show_stack( unsigned long * esp );
extern inline void panic( const char *format, ... ) __attribute__((noreturn));
extern int sema_init( semaphore *sem, int value UNUSE);
extern int down_interruptible( semaphore *sem );
extern void down( semaphore *sem );
extern void up( semaphore *sem );

extern void lock_kernel();
extern void unlock_kernel();

extern void insert_inode_hash(struct inode *inode);
extern int init_special_inode( struct inode *inode, __u32 mode, __u32 rdev );
extern void make_bad_inode( struct inode *inode );
extern int is_bad_inode( struct inode *inode );

struct inode *iget( struct super_block *super, unsigned long ino );
#define I_DIRTY 1
void mark_inode_dirty (struct inode * inode);

/* [cut from include/linux/fs.h]
 * Kernel (and user level) pointers have redundant information, so we
 * can use a scheme where we can return either an error code or a dentry
 * pointer with the same return value.
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
extern inline void *ERR_PTR(long error);
extern inline long PTR_ERR(const void *ptr);
extern inline long IS_ERR(const void *ptr);
extern void *kmalloc( size_t size, int flag UNUSE );
extern void kfree( void *addr );

extern kmem_cache_t *kmem_cache_create( const char *name UNUSE, int size, 
					int a UNUSE, int b UNUSE, int *c UNUSE,
					int *d  UNUSE );
extern int  kmem_cache_destroy( kmem_cache_t *slab );
extern void kmem_cache_free( kmem_cache_t *slab UNUSE, void *addr );
extern void *kmem_cache_alloc( kmem_cache_t *slab, int gfp_flag UNUSE );

extern unsigned long event;
extern char *__prog_name;

/** initialises abend sub-system. Should be called from main directly
    (that is, not from function called from main). */
extern void abendInit( int argc, char **argv );

/** prints current stack trace. Tries to resolve address to symbol name
    either through bfd interfaces or through dlsym. If you see spurious
    `sigaction' in backtrace, read it as __restore() --- function call
    inserted into stack by kernel when vectoring control to the signal
    handler. Problem is that __restore is not exported by libc. */
extern void printBacktrace();

/** abnormal end. Prints backtrace them either dumps core into specified
    directory (/tmp by default --- FIXME-NIKITA Unixism) or tries to attach
    debugger or _exits */
extern void abend() __attribute__((noreturn));

extern void trap_signal( int signum );


/** condition variables: needs to be implemented in the kernel (discuss w/ Nikita) */
typedef struct kcondvar_t kcondvar_t;

struct kcondvar_t
{
  pthread_mutex_t   *_lockp;
  pthread_cond_t     _cond;
  int                _count;
};

/** initialize the condition variable, to be goverened by the supplied lock.  every other
 * call to a kcondvar_ function assumes that the lock is already held. */
extern void	  kcondvar_init      (kcondvar_t        *kcond,
				      spinlock_t        *lock);

/** wait for a signal/broadcast */
extern void       kcondvar_wait      (kcondvar_t        *kcond);

/** return the number of waiters */
extern int        kcondvar_waiters   (kcondvar_t        *kcond);

/** signal a waiter */
extern void       kcondvar_signal    (kcondvar_t        *kcond);

/** broadcast to all waiters */
extern void       kcondvar_broadcast (kcondvar_t        *kcond);

#define DEBUGGING_REISER4_WRITE
#ifdef DEBUGGING_REISER4_WRITE


/* include/asm/page.h */
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~(PAGE_SIZE - 1))


/* include/linux/mm.h */

struct page {
	unsigned long index;
	struct buffer_head * buffers;
	void * virtual;
	struct address_space *mapping;
	unsigned long flags;
	unsigned count;	
	struct list_head list;
};

#define PG_uptodate 1
#define PG_locked 2
#define Page_Uptodate(page) ((page)->flags & PG_uptodate)
#define PageLocked(page) ((page)->flags & PG_locked)
#define UnlockPage(p) \
{\
	assert ("vs-286", PageLocked (p));\
	(p)->flags &= ~PG_locked;\
}
#define LockPage(p) \
{\
	assert ("vs-287", !PageLocked (p));\
	(p)->flags |= PG_locked;\
}
#define SetPageUptodate(page) (page)->flags |= PG_uptodate


/* include/linux/pagemap.h */
struct page * find_get_page (struct address_space *, unsigned long);
struct page * find_lock_page (struct address_space *, unsigned long);
void wait_on_page(struct page * page);
typedef int filler_t(void *, struct page*);

/* mm/page_alloc.c */
void page_cache_release (struct page * page);



/* include/linux/fs.h */
#define READ 0
#define WRITE 1
struct buffer_head {
	unsigned b_size;
	unsigned long long b_blocknr;
	int b_state;
	int b_dev;
	char * b_data;
	struct buffer_head * b_this_page;
	struct block_device * b_bdev;
};
#define BH2_New 1
#define BH2_Mapped 2
#define BH2_Uptodate 4
#define BH2_Lock 8
/* reiser4 should be able to mark buffer_heads corresponding either to
   unallocated or to allocated extents */
#define BH2_unallocated 16
#define BH2_allocated 32
#define BH2_dirty 64
#define buffer_new(bh) ((bh)->b_state & BH2_New)
#define buffer_mapped(bh) ((bh)->b_state & BH2_Mapped)
#define buffer_uptodate(bh) ((bh)->b_state & BH2_Uptodate)
#define buffer_locked(bh) ((bh)->b_state & BH2_Lock)
#define buffer_unallocated(bh) ((bh)->b_state & BH2_unallocated)
#define buffer_allocated(bh) ((bh)->b_state & BH2_allocated)
#define buffer_dirty(bh) ((bh)->b_state & BH2_dirty)

#define mark_buffer_new(bh) ((bh)->b_state |= BH2_New)
#define mark_buffer_mapped(bh) ((bh)->b_state |= BH2_Mapped)
#define mark_buffer_uptodate(bh) ((bh)->b_state |= BH2_Uptodate)
#define mark_buffer_locked(bh) ((bh)->b_state |= BH2_Lock)
#define mark_buffer_unallocated(bh) ((bh)->b_state |= BH2_unallocated)
#define mark_buffer_allocated(bh)  ((bh)->b_state |= BH2_allocated)
#define mark_buffer_dirty(bh)  ((bh)->b_state |= BH2_dirty)

#define lock_buffer(bh) \
{\
	assert ("vs-285", !buffer_locked (bh));\
	mark_buffer_locked(bh);\
}
#define unlock_buffer(bh) \
{\
	assert ("vs-294", buffer_locked (bh));\
	(bh)->b_state &= ~BH2_Lock;\
}

#define make_buffer_uptodate(bh,n) mark_buffer_uptodate(bh)


struct address_space;
struct inode;
typedef int (get_block_t)(struct inode*,long,struct buffer_head*,int);
int create_empty_buffers (struct page * page, unsigned blocksize);
void ll_rw_block(int, int, struct buffer_head * bh[]);
void set_buffer_async_io (struct buffer_head * bh);
int submit_bh (int rw, struct buffer_head * bh);
void map_bh (struct buffer_head * bh, struct super_block * sb,
	     unsigned long long block);
int generic_commit_write(struct file *, struct page *, unsigned, unsigned);

/* include/linux/locks.h */
void wait_on_buffer(struct buffer_head *);

/* include/asm/pgtable.h */
#define flush_dcache_page(page)	do { } while (0)


/* mm/filemap.c */
struct page *grab_cache_page(struct address_space *mapping, unsigned long idx);
struct page *read_cache_page(struct address_space *mapping, unsigned long idx,
			     int (*filler)(void *,struct page*), void *data);

/* fs/buffer.c */


/* include/asm/highmem.h */
char *kmap (struct page * page);
void kunmap (struct page * page UNUSED_ARG);


/* include/asm/uaccess.h */
int __copy_from_user(char *, char *, unsigned);
int __copy_to_user(char *, char *, unsigned);


/* reiserfs */
void mark_journalled (struct buffer_head *);
int is_journalled (struct buffer_head *);
void mark_buffer_allocation_delayed (struct buffer_head *);

extern struct inode *new_inode( struct super_block *sb );
extern struct inode *get_empty_inode();
void iput( struct inode *inode );

void d_add(struct dentry * entry UNUSED_ARG, struct inode * inode UNUSED_ARG);
void d_instantiate(struct dentry *entry, struct inode * inode);

#define	KERN_EMERG	"<0>"	/* system is unusable			*/
#define	KERN_ALERT	"<1>"	/* action must be taken immediately	*/
#define	KERN_CRIT	"<2>"	/* critical conditions			*/
#define	KERN_ERR	"<3>"	/* error conditions			*/
#define	KERN_WARNING	"<4>"	/* warning conditions			*/
#define	KERN_NOTICE	"<5>"	/* normal but significant condition	*/
#define	KERN_INFO	"<6>"	/* informational			*/
#define	KERN_DEBUG	"<7>"	/* debug-level messages			*/

#endif /* debugging reiser4_write */

extern ssize_t pread(int fd, void *buf, size_t count, off_t  offset);
extern ssize_t  pwrite(int  fd,  const  void  *buf, size_t count, off_t offset);

#define UPDATE_ATIME( inode )
#define update_atime( inode )

/*
 * File types
 */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

struct dentry_operations {
	int (*d_revalidate)(struct dentry *, int);
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	int (*d_delete)(struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
};

/* __REISER4_ULEVEL_H__ */
#endif

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
