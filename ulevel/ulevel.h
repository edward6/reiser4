/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * User-level simulation stuff.
 */

#if !defined( __REISER4_ULEVEL_H__ )
#define __REISER4_ULEVEL_H__

#define NOT_YET                        (0)
#define YOU_CAN_COMPILE_PSEUDO_CODE    (0)

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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <limits.h>
#include <dirent.h>

/* for rdtscll() */
#include <asm/msr.h>

/* for __le??_to_cpu() */
#include <linux/byteorder/little_endian.h>

#include <stdio.h>

#include <malloc.h>

/* This let's us test with posix locks, thereby allowing use of typedef'd spinlock_t as
 * the lock guarding our condition variables. */
/*#define KUT_LOCK_POSIX 1*/
/*#define KUT_LOCK_SPINLOCK 1*/
#define KUT_LOCK_ERRORCHECK 1
#define SPINLOCK_BUG(x) spinlock_bug (x);

extern void spinlock_bug (const char *msg);

typedef char __s8;
typedef unsigned char __u8;

typedef short __s16;
typedef unsigned short __u16;

typedef int __s32;
typedef unsigned int __u32;

typedef long long __s64;
typedef unsigned long long __u64;

typedef unsigned umode_t;

#define __cpu_to_le64(x) ((__u64)(x))
#define __le64_to_cpu(x) ((__u64)(x))
#define __cpu_to_le32(x) ((__u32)(x))
#define __le32_to_cpu(x) ((__u32)(x))
#define __cpu_to_le16(x) ((__u16)(x))
#define __le16_to_cpu(x) ((__u16)(x))

#define printk printf

#include "debug.h"

#include "bitops.h"
#include "atomic.h"


#include "../tshash.h"
#include "../reiser4.h"
#include "../forward.h"
#include "../debug.h"

#include "kutlock.h"

#define HAS_BFD                         (1)
#define USE_DLADDR_DLSYM_FOR_BACKTRACE  (1)
#define ULEVEL_DROP_CORE                (0)

#define no_context (0)
#define current_pname   (__prog_name) /* __libc_argv[ 0 ] */
#define current_pid     ( ( int ) pthread_self() )
#define UNUSE __attribute__( ( unused ) )

/*
 * GFP bitmasks..
 */
/* Zone modifiers in GFP_ZONEMASK (see linux/mmzone.h - low four bits) */
#define __GFP_DMA	0x01
#define __GFP_HIGHMEM	0x02

/* Action modifiers - doesn't change the zoning */
#define __GFP_WAIT	0x10	/* Can wait and reschedule? */
#define __GFP_HIGH	0x20	/* Should access emergency pools? */
#define __GFP_IO	0x40	/* Can start low memory physical IO? */
#define __GFP_HIGHIO	0x80	/* Can start high mem physical IO? */
#define __GFP_FS	0x100	/* Can call down to low-level FS? */

#define GFP_NOHIGHIO	(             __GFP_WAIT | __GFP_IO)
#define GFP_NOIO	(             __GFP_WAIT)
#define GFP_NOFS	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO)
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_USER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_HIGHUSER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS | __GFP_HIGHMEM)
#define GFP_KERNEL	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_NFS		(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_KSWAPD	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA

#define	SLAB_KERNEL		GFP_KERNEL
#define SLAB_HWCACHE_ALIGN (0)
/* flags passed to a constructor func */
#define	SLAB_CTOR_CONSTRUCTOR	0x001UL /* if not set, then deconstructor */
#define SLAB_CTOR_ATOMIC	0x002UL	/* tell constructor it can't sleep */
#define	SLAB_CTOR_VERIFY	0x004UL	/* tell constructor it's a verify call */

#define CURRENT_TIME				\
({						\
	struct timespec foo;			\
						\
	foo.tv_sec = time(0);			\
	foo.tv_nsec = 0;			\
	foo;					\
})

#define likely( x ) ( x )
#define unlikely( x ) ( x )
#define BUG() do { printf ("BUG! help!\n"); abort (); } while (0)

#define S_IRWXUGO	(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO		(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO		(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO		(S_IXUSR|S_IXGRP|S_IXOTH)

#define __init
#define __exit

#define module_init(a)				\
void run_##a ()					\
{						\
  a();						\
}

#define module_exit(b)				\
void run_##b ()					\
{						\
  b();						\
}

#define MODULE_DESCRIPTION(a)
#define MODULE_AUTHOR(a)
#define MODULE_LICENSE(a)

#define THIS_MODULE (NULL)
#define FS_REQUIRES_DEV (0)

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


#define MEMORY_PRESSURE_THRESHOLD   (5000000)/*(1000)*/
#define MEMORY_PRESSURE_HOWMANY     (100)

/* from <linux/list.h> */

#define wmb()

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
static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
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
static inline void list_add(struct list_head *new, struct list_head *head)
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
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __inline__ void __list_add_rcu(struct list_head * new,
	struct list_head * prev,
	struct list_head * next)
{
	new->next = next;
	new->prev = prev;
	wmb();
	next->prev = new;
	prev->next = new;
}

/**
 * list_add_rcu - add a new entry to rcu-protected list
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __inline__ void list_add_rcu(struct list_head *new, struct list_head *head)
{
	__list_add_rcu(new, head, head->next);
}

/**
 * list_add_tail_rcu - add a new entry to rcu-protected list
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __inline__ void list_add_tail_rcu(struct list_head *new, struct list_head *head)
{
	__list_add_rcu(new, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}
/**
 * list_del_rcu - deletes entry from list without re-initialization
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this,
 * the entry is in an undefined state. It is useful for RCU based
 * lockfree traversal.
 */
static inline void list_del_rcu(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

/**
 * list_move - delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static inline void list_move(struct list_head *list, struct list_head *head)
{
        __list_del(list->prev, list->next);
        list_add(list, head);
}

/**
 * list_move_tail - delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static inline void list_move_tail(struct list_head *list,
				  struct list_head *head)
{
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int list_empty(struct list_head *head)
{
	return head->next == head;
}

static inline void __list_splice(struct list_head *list,
				 struct list_head *head)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;
	struct list_head *at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;
}

/**
 * list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void list_splice(struct list_head *list, struct list_head *head)
{
	if (!list_empty(list))
		__list_splice(list, head);
}

/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static inline void list_splice_init(struct list_head *list,
				    struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head);
		INIT_LIST_HEAD(list);
	}
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
        	pos = pos->next, prefetch(pos->next))

/**
 * __list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 *
 * This variant differs from list_for_each() in that it's the
 * simplest possible list iteration code, no prefetching is done.
 * Use this for code that knows the list to be very short (empty
 * or 1 entry) most of the time.
 */
#define __list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_prev	-	iterate over a list backwards
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev, prefetch(pos->prev); pos != (head); \
        	pos = pos->prev, prefetch(pos->prev))
        	
/**
 * list_for_each_safe	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop counter.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop counter.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		     prefetch(pos->member.next);			\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member),	\
		     prefetch(pos->member.next))

/**
 * list_for_each_rcu	-	iterate over an rcu-protected list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each_rcu(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
        	pos = pos->next, ({ read_barrier_depends(); 0;}), prefetch(pos->next))
        	
#define __list_for_each_rcu(pos, head) \
	for (pos = (head)->next; pos != (head); \
        	pos = pos->next, ({ read_barrier_depends(); 0;}))
        	
/**
 * list_for_each_safe_rcu	-	iterate over an rcu-protected list safe
 *					against removal of list entry
 * @pos:	the &struct list_head to use as a loop counter.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe_rcu(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, ({ read_barrier_depends(); 0;}), n = pos->next)


/* end of <linux/list.h> */


/* include/linux/radix-tree.h */
struct radix_tree_root {
	void * vp;
	/*pc_hash_table page_ht;*/
};

extern void *radix_tree_lookup(struct radix_tree_root *, unsigned long);


/* include/linux/blkdev.h */
typedef struct request_queue
{
	unsigned short max_sectors;
} request_queue_t;

struct block_device;
request_queue_t *bdev_get_queue(struct block_device *bdev);



static inline void prefetch(const void *x UNUSED_ARG ) {;}

extern pthread_key_t __current_key;

extern __u32 set_current ();

#define current ((struct task_struct*) pthread_getspecific (__current_key))
	

typedef struct semaphore {
	pthread_mutex_t  mutex;
} semaphore;

struct rw_semaphore {
	semaphore sem;
};

typedef struct kmem_cache_t kmem_cache_t;

struct kmem_cache_t {
	size_t      size;
	const char* name;
	unsigned    count;
	spinlock_t  lock;
	void (*ctor)(void*, kmem_cache_t *, unsigned long);
	void (*dtor)(void*, kmem_cache_t *, unsigned long);
};

/* quick string, with pre-computed hash used to speedup comparisons */
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
	struct qstr d_name;	/* quick string, with pre-computed hash used to speedup comparisons */
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

struct vfsmount {
	struct super_block *mnt_sb;
};

/*
 * Track a single file's readahead state
 */
struct file_ra_state {
	unsigned long start;		/* Current window */
	unsigned long size;
	unsigned long next_size;	/* Next window size */
	unsigned long prev_page;	/* Cache last read() position */
	unsigned long ahead_start;	/* Ahead window */
	unsigned long ahead_size;
	unsigned long ra_pages;		/* Maximum readahead window */
};

struct file {
	struct list_head	f_list;
	struct dentry		*f_dentry;
	struct vfsmount         *f_vfsmnt;
	struct file_operations	*f_op;
	struct file_ra_state	f_ra;
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
	int                     f_ufd;
};


typedef int (*filldir_t)(void *, const char *, int, loff_t, ino_t, unsigned);
struct poll_table_struct;
struct vm_area_struct;
struct file_lock;
struct iovec;
struct page;
struct nameidata;
struct kiocb;

struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char *, size_t, loff_t *);
	ssize_t (*aio_read) (struct kiocb *, char *, size_t, loff_t);
	ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
	ssize_t (*aio_write) (struct kiocb *, char *, size_t, loff_t);
	int (*readdir) (struct file *, void *, filldir_t);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int datasync);
	int (*aio_fsync) (struct kiocb *, int datasync);
	int (*fasync) (int, struct file *, int);
	int (*lock) (struct file *, int, struct file_lock *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long, loff_t *);
	ssize_t (*sendfile) (struct file *, struct file *, loff_t *, size_t);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, loff_t *, int);
	unsigned long (*get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
};

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512	/* Not a change, but a change it */
#define ATTR_ATTR_FLAG	1024
#define ATTR_KILL_SUID	2048
#define ATTR_KILL_SGID	4096

/*
 * This is the Inode Attributes structure, used for notify_change().  It
 * uses the above definitions as flags, to know which values have changed.
 * Also, in this manner, a Filesystem can look at only the values it cares
 * about.  Basically, these are the attributes that the VFS layer can
 * request to change from the FS layer.
 *
 * Derek Atkins <warlord@MIT.EDU> 94-10-20
 */
struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	loff_t		ia_size;
	struct timespec	ia_atime;
	struct timespec ia_mtime;
	struct timespec ia_ctime;
	unsigned int	ia_attr_flags;
};

struct kstat;

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
	int (*setattr) (struct dentry *, struct iattr *);
	int (*getattr) (struct vfsmount *mnt, struct dentry *, struct kstat *);
	int (*setxattr) (struct dentry *, const char *, void *, size_t, int);
	ssize_t (*getxattr) (struct dentry *, const char *, void *, size_t);
	ssize_t (*listxattr) (struct dentry *, char *, size_t);
	int (*removexattr) (struct dentry *, const char *);
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
   	struct inode *(*alloc_inode)(struct super_block *sb);
	void (*destroy_inode)(struct inode *);

	void (*read_inode) (struct inode *);

   	void (*dirty_inode) (struct inode *);
	void (*write_inode) (struct inode *, int);
	void (*put_inode) (struct inode *);
	void (*drop_inode) (struct inode *);
	void (*delete_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	void (*write_super) (struct super_block *);
	void (*write_super_lockfs) (struct super_block *);
	void (*unlockfs) (struct super_block *);
	int (*statfs) (struct super_block *, struct statfs *);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*clear_inode) (struct inode *);
	void (*umount_begin) (struct super_block *);

	int (*show_options)(struct seq_file *, struct vfsmount *);
};

extern int seq_printf(struct seq_file *, const char *, ...)
	__attribute__ ((format (printf,2,3)));


struct signal_struct {
	spinlock_t		siglock;
};

/*
 * Some file systems need context associated with current thread during
 * one system call (transaction handle, for example). This context in
 * attached to current->fs_context.
 *
 * As it is possible for file system calls to nest (through quota of VM
 * call backs), every file system using current->fs_context should store
 * original ->fs_context value of entrance and restore in on exit.
 */
struct fs_activation {
	/*
	 * cookie allowing to distinguish file system instances
	 * (mounts). Usually this is pointer to the super block, but not
	 * necessary. This is used to tell reentrance.
	 */
	void *owner;
};

struct task_struct {
	char comm[ 30 ];
	int   pid;
/* info about current file system activation */
	struct fs_activation *fs_context;
	__u32         fsuid;
	__u32         fsgid;
	int i_am_swapd; /**/
	int flags;
	struct signal_struct *sig;
	struct signal_struct sig_here;
};

#define PF_MEMALLOC 1 /* NOTE: Not currently set in ulevel.  Should be set in task_struct->flags. */
#define PF_FREEZE   2

struct block_device {
	int bd_dev;
	struct request_queue * bd_queue;
	void * vp;
	__u64 last_sector;
};

typedef unsigned short kdev_t;

#define val_to_kdev(val) val
#define kdev_val(kdev) kdev

struct super_block {
	kdev_t			s_dev;
	unsigned char s_dirt;
	struct block_device   * s_bdev;
	struct file_system_type *s_type;
	unsigned long s_blocksize;
	unsigned char s_blocksize_bits;
	struct dentry *s_root;
	struct super_operations *s_op;
	unsigned long s_flags;
	char * s_id;
	void 			*s_fs_info;	/* Filesystem private info */
};

struct address_space;
struct kiobuf;

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	WB_SYNC_NONE,	/* Don't wait on anything */
	WB_SYNC_ALL,	/* Wait on every mapping */
	WB_SYNC_HOLD,	/* Hold the inode on sb_dirty for sys_sync() */
};

struct backing_dev_info;

/*
 * A control structure which tells the writeback code what to do
 */
struct writeback_control {
	struct backing_dev_info *bdi;	/* If !NULL, only write back this
					   queue */
	enum writeback_sync_modes sync_mode;
	unsigned long *older_than_this;	/* If !NULL, only write back inodes
					   older than this */
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	unsigned int gfp_mask;
	int nonblocking;		/* Don't get stuck on request queues */
	int encountered_congestion;	/* An output: a queue is full */
};


struct address_space_operations {
	int (*writepage)(struct page *);
	int (*readpage)(struct file *, struct page *);
	int (*sync_page)(struct page *);

	/* Write back some dirty pages from this mapping. */
	int (*writepages)(struct address_space *, struct writeback_control *wbc);

	/* Set a page dirty */
	int (*set_page_dirty)(struct page *page);

	int (*readpages)(struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages);

	/*
	 * ext3 requires that a successful prepare_write() call be followed
	 * by a commit_write() call - they must be balanced
	 */
	int (*prepare_write)(struct file *, struct page *, unsigned, unsigned);
	int (*commit_write)(struct file *, struct page *, unsigned, unsigned);
	/* Unfortunately this kludge is needed for FIBMAP. Don't use it */
	int (*bmap)(struct address_space *, long);
	int (*invalidatepage) (struct page *, unsigned long);
	int (*releasepage) (struct page *, int);
	int (*direct_IO)(int, struct inode *, char *buf,
				loff_t offset, size_t count);
};

enum bdi_state {
	BDI_pdflush,		/* A pdflush thread is working this device */
	BDI_write_congested,	/* The write queue is getting full */
	BDI_read_congested,	/* The read queue is getting full */
	BDI_write_active,	/* There are one or more queued writes */
	BDI_read_active,	/* There are one or more queued reads */
	BDI_unused,		/* Available bits start here */
};

struct backing_dev_info {
	unsigned long ra_pages;	/* max readahead in PAGE_CACHE_SIZE units */
	unsigned long state;	/* Always use atomic bitops on this */
	int memory_backed;	/* Cannot clean pages with writepage */
};


struct address_space {
	struct list_head	clean_pages;	/* list of clean pages */
	struct list_head	dirty_pages;	/* list of dirty pages */
	struct list_head	locked_pages;	/* list of locked pages */
	struct list_head	io_pages;	/* list of locked pages */
	unsigned long		nrpages;	/* number of total pages */
	struct address_space_operations *a_ops;	/* methods */
	struct inode		*host;		/* owner: inode, block_device */
	struct vm_area_struct	*i_mmap;	/* list of private mappings */
	struct vm_area_struct	*i_mmap_shared; /* list of shared mappings */
	spinlock_t		i_shared_lock;  /* and spinlock protecting it */
	int			gfp_mask;	/* how to allocate the pages */

	struct radix_tree_root  page_tree;      /* pages attached to mapping */
	spinlock_t              page_lock;      /* lock protecting
						 * adding/removing/searching
						 * pages*/
	spinlock_t		private_lock;	/* for use by the address_space */
	struct list_head	private_list;	/* ditto */
	struct address_space	*assoc_mapping;	/* ditto */
	struct backing_dev_info *backing_dev_info; /* device readahead, etc */
};

struct block_device;
typedef __u64 sector_t;

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
	struct timespec		i_atime;
	struct timespec		i_mtime;
	struct timespec		i_ctime;
	unsigned int		i_blkbits;
	unsigned long		i_blksize;
	unsigned long		i_blocks;
	unsigned short		i_bytes;
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

	struct block_device    *i_bdev;
	atomic_t		i_writecount;
	__u32			i_generation;
	union {
		void				*generic_ip;
	} u;
};

struct root_dir {
	struct inode *d_inode;
};

extern unsigned long get_jiffies ();

#define jiffies get_jiffies()
#define signal_pending( current ) (0)

extern void dump_stack( void );
extern void show_stack( unsigned long * esp );
extern void panic( const char *format, ... ) __attribute__((noreturn));
extern int sema_init( semaphore *sem, int value UNUSE);
extern int init_MUTEX_LOCKED( semaphore *sem );
extern int down_interruptible( semaphore *sem );
extern void down( semaphore *sem );
extern void up( semaphore *sem );

static inline void down_read (struct rw_semaphore * rwsem)
{
	down (&rwsem->sem);
}

static inline void up_read (struct rw_semaphore * rwsem)
{
	up (&rwsem->sem);
}

static inline void down_write (struct rw_semaphore * rwsem)
{
	down (&rwsem->sem);
}

static inline void up_write (struct rw_semaphore * rwsem)
{
	up (&rwsem->sem);
}

extern void lock_kernel();
extern void unlock_kernel();

extern void insert_inode_hash(struct inode *inode);
extern int init_special_inode( struct inode *inode, __u32 mode, int rdev );
extern void make_bad_inode( struct inode *inode );
extern int is_bad_inode( struct inode *inode );

extern struct inode *
iget5_locked(struct super_block *sb,
	     unsigned long hashval,
	     int (*test)(struct inode *, void *),
	     int (*set)(struct inode *, void *), void *data);
extern struct inode *iget_locked(struct super_block *sb, unsigned long ino);
extern struct inode * ilookup5(struct super_block * sb, unsigned long ino, int (*test)(struct inode *, void *), void *data);

#define I_DIRTY    0x1
#define I_NEW      0x2
#define I_LOCK     0x4
#define I_DIRTY_PAGES 0x8
#define I_FREEING  0x10
#define I_CLEAR    0x20

void mark_inode_dirty (struct inode * inode);

/* [cut from include/linux/fs.h]
 * Kernel (and user level) pointers have redundant information, so we
 * can use a scheme where we can return either an error code or a dentry
 * pointer with the same return value.

Isn't this bad style?  Please discuss with me.  NIKITA-FIXME-HANS
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
extern void *kmalloc( size_t size, int flag UNUSE );
extern void kfree( void *addr );

static inline void *ERR_PTR(long error)
{
	return (void *) error;
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

static inline long IS_ERR(const void *ptr)
{
	return (unsigned long)ptr > (unsigned long)-1000L;
}


extern kmem_cache_t *kmem_cache_create( const char *name,
				 size_t size UNUSED_ARG,
				 size_t offset UNUSED_ARG,
				 unsigned long flags UNUSED_ARG,
				 void (*ctor)(void*, kmem_cache_t *, unsigned long) UNUSED_ARG,
				 void (*dtor)(void*, kmem_cache_t *, unsigned long) UNUSED_ARG );
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

#define DEBUGGING_REISER4_WRITE
#ifdef DEBUGGING_REISER4_WRITE

/* include/linux/mm.h */

/** declare hash table of znodes */
TS_HASH_DECLARE(mp, struct page);

struct page {
	unsigned long index;
	struct address_space *mapping;
	void * virtual;
	unsigned long flags;
	atomic_t count;
	unsigned long private;
	struct list_head list; /* either mapping's clean, dirty or locked
				* list */
	struct list_head lru;  /* global page list */
	spinlock_t lock;
	spinlock_t lock2;
	int kmap_count;
	struct page *self;
	mp_hash_link link; /* link to mapping */
};

#define PG_locked	         0	/* Page is locked. Don't touch. */
#define PG_error		 1
#define PG_referenced		 2
#define PG_uptodate		 3

#define PG_dirty        	 4
#define PG_lru			 5
#define PG_active		 6
#define PG_slab			 7	/* slab debug (Suparna wants this) */

#define PG_highmem		 8
#define PG_checked		 9	/* kill me in 2.5.<early>. */
#define PG_arch_1		10
#define PG_reserved		11

#define PG_private		12	/* Has something at ->private */
#define PG_writeback		13	/* Page is under writeback */
#define PG_nosave		15	/* Used for system suspend/resume */

#define inc_page_state(member)	noop
#define dec_page_state(member)	noop

/*
 * Manipulation of page state flags
 */
#define PageLocked(page)		\
		test_bit(PG_locked, &(page)->flags)
#define SetPageLocked(page)		\
		set_bit(PG_locked, &(page)->flags)
#define TestSetPageLocked(page)				\
	({						\
		int ret;				\
		ret = spin_trylock (&(page)->lock);	\
		if(ret)					\
			SetPageLocked(page);		\
		!ret;					\
	})
#define ClearPageLocked(page)		\
		clear_bit(PG_locked, &(page)->flags)
#define TestClearPageLocked(page)	\
		test_and_clear_bit(PG_locked, &(page)->flags)

#define PageError(page)		test_bit(PG_error, &(page)->flags)
#define SetPageError(page)	set_bit(PG_error, &(page)->flags)
#define ClearPageError(page)	clear_bit(PG_error, &(page)->flags)

#define PageReferenced(page)	test_bit(PG_referenced, &(page)->flags)
#define SetPageReferenced(page)	set_bit(PG_referenced, &(page)->flags)
#define ClearPageReferenced(page)	clear_bit(PG_referenced, &(page)->flags)
#define TestClearPageReferenced(page) test_and_clear_bit(PG_referenced, &(page)->flags)

#define PageUptodate(page)	test_bit(PG_uptodate, &(page)->flags)
#define SetPageUptodate(page)	set_bit(PG_uptodate, &(page)->flags)
#define ClearPageUptodate(page)	clear_bit(PG_uptodate, &(page)->flags)

#define PageDirty(page)		test_bit(PG_dirty, &(page)->flags)
#define SetPageDirty(page)						\
	do {								\
		if (!test_and_set_bit(PG_dirty,			\
					&(page)->flags))		\
			inc_page_state(nr_dirty);			\
	} while (0)
#define TestSetPageDirty(page)						\
	({								\
		int ret;						\
		ret = test_and_set_bit(PG_dirty,		\
				&(page)->flags);			\
		if (!ret)						\
			inc_page_state(nr_dirty);			\
		ret;							\
	})
#define ClearPageDirty(page)						\
	do {								\
		if (test_and_clear_bit(PG_dirty,		\
				&(page)->flags))			\
			dec_page_state(nr_dirty);			\
	} while (0)
#define TestClearPageDirty(page)					\
	({								\
		int ret;						\
		ret = test_and_clear_bit(PG_dirty,		\
				&(page)->flags);			\
		if (ret)						\
			dec_page_state(nr_dirty);			\
		ret;							\
	})

#define PageLRU(page)		test_bit(PG_lru, &(page)->flags)
#define TestSetPageLRU(page)	test_and_set_bit(PG_lru, &(page)->flags)
#define TestClearPageLRU(page)	test_and_clear_bit(PG_lru, &(page)->flags)

#define PageActive(page)	test_bit(PG_active, &(page)->flags)
#define SetPageActive(page)	set_bit(PG_active, &(page)->flags)
#define ClearPageActive(page)	clear_bit(PG_active, &(page)->flags)

#define PageSlab(page)		test_bit(PG_slab, &(page)->flags)
#define SetPageSlab(page)	set_bit(PG_slab, &(page)->flags)
#define ClearPageSlab(page)	clear_bit(PG_slab, &(page)->flags)

#ifdef CONFIG_HIGHMEM
#define PageHighMem(page)	test_bit(PG_highmem, &(page)->flags)
#else
#define PageHighMem(page)	0 /* needed to optimize away at compile time */
#endif

#define PageChecked(page)	test_bit(PG_checked, &(page)->flags)
#define SetPageChecked(page)	set_bit(PG_checked, &(page)->flags)

#define PageReserved(page)	test_bit(PG_reserved, &(page)->flags)
#define SetPageReserved(page)	set_bit(PG_reserved, &(page)->flags)
#define ClearPageReserved(page)	clear_bit(PG_reserved, &(page)->flags)

#define SetPagePrivate(page)	set_bit(PG_private, &(page)->flags)
#define ClearPagePrivate(page)	clear_bit(PG_private, &(page)->flags)
#define PagePrivate(page)	test_bit(PG_private, &(page)->flags)

#define PageWriteback(page)	test_bit(PG_writeback, &(page)->flags)
#define SetPageWriteback(page)						\
	do {								\
		if (!test_and_set_bit(PG_writeback,			\
				&(page)->flags))			\
			inc_page_state(nr_writeback);			\
	} while (0)
#define TestSetPageWriteback(page)					\
	({								\
		int ret;						\
		ret = test_and_set_bit(PG_writeback,			\
					&(page)->flags);		\
		if (!ret)						\
			inc_page_state(nr_writeback);			\
		ret;							\
	})
#define ClearPageWriteback(page)					\
	do {								\
		if (test_and_clear_bit(PG_writeback,			\
				&(page)->flags))			\
			dec_page_state(nr_writeback);			\
	} while (0)
#define TestClearPageWriteback(page)					\
	({								\
		int ret;						\
		ret = test_and_clear_bit(PG_writeback,			\
				&(page)->flags);			\
		if (ret)						\
			dec_page_state(nr_writeback);			\
		ret;							\
	})

#define PageNosave(page)	test_bit(PG_nosave, &(page)->flags)
#define SetPageNosave(page)	set_bit(PG_nosave, &(page)->flags)
#define TestSetPageNosave(page)	test_and_set_bit(PG_nosave, &(page)->flags)
#define ClearPageNosave(page)		clear_bit(PG_nosave, &(page)->flags)
#define TestClearPageNosave(page)	test_and_clear_bit(PG_nosave, &(page)->flags)

#define page_address(page)                                              \
	({								\
		assert ("vs-694", page->kmap_count > 0);			\
		(page)->virtual;					\
	})


void remove_inode_page(struct page *);
void page_cache_readahead(struct address_space *mapping,
			  struct file_ra_state *ra,
			  struct file *filp, unsigned long offset);


/* include/linux/pagemap.h */
struct page * find_get_page (struct address_space *, unsigned long);
struct page * find_lock_page (struct address_space *, unsigned long);
void wait_on_page_locked(struct page * page);
typedef int filler_t(void *, struct page*);
void lock_page(struct page *page);
void unlock_page(struct page *page);
struct page *page_cache_alloc (struct address_space *);
extern int add_to_page_cache_unique(struct page *, struct address_space *,
				    unsigned long index);
extern int add_to_page_cache (struct page * page,
			      struct address_space * mapping,
			      unsigned long offset);

/* include/linux/swap.h */
void lru_cache_del(struct page *);

/* mm/page_alloc.c */
void page_cache_get(struct page * page);
void page_cache_release (struct page * page);



/* include/linux/fs.h */
#define READ 0
#define WRITE 1
struct buffer_head {
	size_t b_size;
	unsigned long long b_blocknr;
	int b_state;
	int b_dev;
	char * b_data;
	struct buffer_head * b_this_page;
	struct block_device * b_bdev;
	int b_count;
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

#define set_buffer_uptodate(bh) ((bh)->b_state |= BH2_Uptodate)
#define clear_buffer_uptodate(bh) ((bh)->b_state &= ~BH2_Uptodate)

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
typedef int (get_block_t)(struct inode*,sector_t,struct buffer_head*,int);

/*int create_empty_buffers (struct page * page, unsigned blocksize,
  unsigned long b_state);*/
void ll_rw_block(int, int, struct buffer_head * bh[]);
void mark_buffer_async_read (struct buffer_head * bh);
int submit_bh (int rw, struct buffer_head * bh);
void map_bh (struct buffer_head * bh, struct super_block * sb,
	     unsigned long long block);
int generic_commit_write(struct file *, struct page *, unsigned, unsigned);
int generic_file_mmap(struct file * file, struct vm_area_struct * vma);


/* include/linux/buffer_head.h */
#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)

#define page_buffers(page)                                      \
        ({                                                      \
                if (!PagePrivate(page))                         \
                        BUG();                                  \
                ((struct buffer_head *)(page)->private);        \
        })
#define page_has_buffers(page)  PagePrivate(page)
#define set_page_buffers(page, buffers)                         \
        do {                                                    \
                SetPagePrivate(page);                           \
                page->private = (unsigned long)buffers;         \
        } while (0)

int fsync_bdev(struct block_device *);
sector_t generic_block_bmap(struct address_space *, sector_t, get_block_t *);

/* include/linux/locks.h */
void wait_on_buffer(struct buffer_head *);

/* include/asm/pgtable.h */
#define flush_dcache_page(page)	do { } while (0)

/* include/asm/page.h */
#define virt_to_page(addr) ((struct page *)((char *)addr - sizeof (struct page)))

/* mm/filemap.c */
struct page *grab_cache_page(struct address_space *mapping, unsigned long idx);
struct page *read_cache_page(struct address_space *mapping, unsigned long idx,
			     int (*filler)(void *,struct page*), void *data);

struct page * find_or_create_page (struct address_space * mapping,
				   unsigned long ind, int gfp);

/* fs/buffer.c */
struct buffer_head * sb_bread (struct super_block * sb, int block);
void brelse (struct buffer_head *);

/* include/linux/dcache.h */
struct dentry * d_alloc_root(struct inode *);

/* fs/block_dev.c */
int sb_set_blocksize(struct super_block *, int);

/* include/asm/highmem.h */
char *kmap (struct page * page);
void kunmap (struct page * page UNUSED_ARG);

#define kmap_atomic(p,f) kmap(p)
#define kunmap_atomic(p,f) kunmap(p)

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
/*
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14
*/
struct dentry_operations {
	int (*d_revalidate)(struct dentry *, int);
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	int (*d_delete)(struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
};

static inline int vfs_permission(struct inode * inode UNUSED_ARG,
				 int mask UNUSED_ARG)
{
	return 0;
}

/*
 * These are initializations that only need to be done
 * once, because the fields are idempotent across use
 * of the inode, so let the slab aware of that.
 */
static inline void inode_init_once(struct inode *inode)
{
	memset(inode, 0, sizeof(*inode));
}

struct module;
struct file_system_type {
	struct module *owner;
	const char *name;
	struct super_block *(*get_sb) (struct file_system_type *, int, char *, void *);
	void (*kill_sb) (struct super_block *);
	struct file_system_type * next;
	struct list_head fs_supers;
	int fs_flags;
};

struct super_block *get_sb_bdev(struct file_system_type *fs_type,
        int flags, char *dev_name, void * data,
        int (*fill_super)(struct super_block *, void *, int));


int invalidate_inodes (struct super_block *sb);

static inline void generic_shutdown_super(struct super_block *sb)
{
	struct dentry *root = sb->s_root;
	struct super_operations *sop = sb->s_op;

	if (root) {
		iput(root->d_inode);
		sb->s_root = NULL;
		/* bad name - it should be evict_inodes() */
		invalidate_inodes(sb);
		if (sop) {
			if (sop->write_super && sb->s_dirt)
				sop->write_super(sb);
			if (sop->put_super)
				sop->put_super(sb);
		}

		/* Forget any remaining inodes */
		if (invalidate_inodes(sb)) {
			printk("VFS: Busy inodes after unmount. "
			   "Self-destruct in 5 seconds.  Have a nice day...\n");
		}
	}
}

static inline void kill_block_super(struct super_block *sb UNUSED_ARG)
{
	generic_shutdown_super(sb);
}

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type * fs);

int block_read_full_page(struct page *page, get_block_t *get_block);

static inline void mark_page_accessed( struct page *page UNUSED_ARG )
{
}

static inline void unlock_new_inode(struct inode *inode)
{
	/*
	 * This is special!  We do not need the spinlock
	 * when clearing I_LOCK, because we're guaranteed
	 * that nobody else tries to do anything about the
	 * state of the inode when it is locked, as we
	 * just created it (so there can be no old holders
	 * that haven't tested I_LOCK).
	 */
	inode->i_state &= ~(I_LOCK|I_NEW);
}

extern void declare_memory_pressure( void );


extern int PAGE_CACHE_SHIFT;
extern unsigned long PAGE_CACHE_SIZE;
extern unsigned long PAGE_CACHE_MASK;
/*
#define PAGE_CACHE_SHIFT	12
#define PAGE_CACHE_SIZE	(1UL << PAGE_CACHE_SHIFT)
#define PAGE_CACHE_MASK	(~(PAGE_CACHE_SIZE-1))
*/
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

enum bh_state_bits {
	BH_Uptodate,	/* 1 if the buffer contains valid data */
	BH_Dirty,	/* 1 if the buffer is dirty */
	BH_Lock,	/* 1 if the buffer is locked */
	BH_Req,		/* 0 if the buffer has been invalidated */
	BH_Mapped,	/* 1 if the buffer has a disk mapping */
	BH_New,		/* 1 if the buffer is new and not yet written out */
	BH_Async,	/* 1 if the buffer is under end_buffer_io_async I/O */
	BH_Wait_IO,	/* 1 if we should write out this buffer */
	BH_launder,	/* 1 if we should throttle on this buffer */
	BH_JBD,		/* 1 if it has an attached journal_head */

	BH_PrivateStart,/* not a state bit, but the first bit available
			 * for private allocation by other entities
			 */
};

/* include/linux/bio.h */
struct bio;
typedef int (bio_end_io_t) (struct bio *, unsigned int bytes_done, int err);
typedef void (bio_destructor_t) (struct bio *);

struct bio_vec {
	struct page	*bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

struct bio
{
	struct bio_vec *bi_io_vec;	/* the actual vec list */
	unsigned short  bi_vcnt;	/* how many bio_vec's */
	unsigned long	bi_flags;	/* status, command, etc */
	sector_t		bi_sector;
	struct block_device	*bi_bdev;
	unsigned short		bi_idx;		/* current index into bvl_vec */
	unsigned int		bi_size;	/* residual I/O count */
	bio_end_io_t		*bi_end_io;
	void                    *bi_private;
};

#define BIO_MAX_SECTORS	128
#define BIO_MAX_SIZE	(BIO_MAX_SECTORS << 9)

/*
 * bio flags
 */
#define BIO_UPTODATE	0	/* ok after I/O completion */
#define BIO_RW_BLOCK	1	/* RW_AHEAD set, and read/write would block */
#define BIO_EOF		2	/* out-out-bounds error */
#define BIO_SEG_VALID	3	/* nr_hw_seg valid */
#define BIO_CLONED	4	/* doesn't own data */

/*
 * bio bi_rw flags
 *
 * bit 0 -- read (not set) or write (set)
 * bit 1 -- rw-ahead when set
 * bit 2 -- barrier
 */
#define BIO_RW		0
#define BIO_RW_AHEAD	1
#define BIO_RW_BARRIER	2

int submit_bio (int rw, struct bio *bio);

static inline struct bio* bio_alloc (int gfp_flag, int vec_size)
{
	struct bio     *bio = kmalloc (sizeof (struct bio), gfp_flag);
	struct bio_vec *vec = kmalloc (sizeof (struct bio_vec) * vec_size, gfp_flag);

	if (bio == NULL || vec == NULL) {
		if (bio != NULL) { kfree (bio); }
		if (vec != NULL) { kfree (vec); }
		return NULL;
	}

	bio->bi_io_vec = vec;
	bio->bi_vcnt = 0;
	return bio;
}

static inline void bio_put (struct bio *bio)
{
	kfree (bio->bi_io_vec);
	kfree (bio);
}

/* include/linux/stat.h */
struct kstat {
        unsigned long   ino;
        dev_t           dev;
        umode_t         mode;
        nlink_t         nlink;
        uid_t           uid;
        gid_t           gid;
        dev_t           rdev;
        loff_t          size;
	struct timespec		atime;
	struct timespec		mtime;
	struct timespec		ctime;
        unsigned long   blksize;
        unsigned long   blocks;
};

#define get_unaligned(ptr) (*(ptr))
#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#define to_kdev_t( x ) ( x )
#define kdev_t_to_nr( x ) ( x )

static inline void init_rwsem( struct rw_semaphore *rwsem )
{
	sema_init( &rwsem -> sem, 1 );
}

int __set_page_dirty_nobuffers(struct page *page);

static inline int __set_page_dirty_buffers(struct page *page UNUSED_ARG)
{
	return 0;
}

static inline int set_page_dirty(struct page *page)
{
	if (page->mapping) {
		int (*spd)(struct page *);

		spd = page->mapping->a_ops->set_page_dirty;
		if (spd)
			return (*spd)(page);
	}
	return __set_page_dirty_buffers(page);
}

extern int write_one_page(struct page *page, int wait);

extern void wait_on_page_locked(struct page *page);
extern void wait_on_page_writeback(struct page *page);
extern void end_page_writeback(struct page *page);

/* include/linux/spinlock.h */
/*
 * FIXME-VS: read_lock and read_unlock are used around radix_tree_lookup
 */
static inline void read_lock (spinlock_t * lock)
{
	spin_lock (lock);
}

static inline void read_unlock (spinlock_t * lock)
{
	spin_unlock (lock);
}

static inline void write_lock (spinlock_t * lock)
{
	spin_lock (lock);
}

static inline void write_unlock (spinlock_t * lock)
{
	spin_unlock (lock);
}

extern int block_sync_page(struct page *page);
extern void blk_run_queues (void);

static inline int page_count( const struct page *page )
{
	return atomic_read( &page -> count );
}

extern int inode_setattr( struct inode * inode, struct iattr * attr );

#define DQUOT_TRANSFER( a, b ) (0)

extern int inode_change_ok( struct inode *inode, struct iattr *attr );

#define ____cacheline_aligned_in_smp

extern loff_t default_llseek( struct file *, loff_t, int );

extern int vfs_readlink(struct dentry *dentry, char *buffer,
		 int buflen, const char *link);

extern int vfs_follow_link(struct nameidata *nd, const char *link);

extern void balance_dirty_pages(struct address_space *mapping);

struct timer_list {
	struct list_head list;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
};

extern void add_timer(struct timer_list * timer);
extern int del_timer(struct timer_list * timer);
#define del_timer_sync(t)	del_timer(t)

static inline void init_timer(struct timer_list * timer)
{
	timer->list.next = timer->list.prev = NULL;
}

#define HZ (100)

#define time_after(a,b)		((long)(b) - (long)(a) < 0)
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)	((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)	time_after_eq(b,a)

static inline void schedule( void )
{
	sched_yield();
}

#define daemonize()
#define spin_lock_irq   spin_lock
#define spin_unlock_irq spin_unlock
#define siginitsetinv( a, b )
#define recalc_sigpending() noop
#define refrigerator( f ) noop

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/**
 * container_of - cast a member of a structure out to the containing structure
NIKITA-FIXME-HANS: that means what?  why is this in this file?
 *
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
        const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define reparent_to_init() noop

/* Inode flags */

#define S_SYNC		1	/* Writes are synced at once */
#define S_NOATIME	2	/* Do not update access times */
#define S_QUOTA		4	/* Quota initialized for file */
#define S_APPEND	8	/* Append-only file */
#define S_IMMUTABLE	16	/* Immutable file */
#define S_DEAD		32	/* removed, but still open directory */
#define S_NOQUOTA	64	/* Inode is not counted to quota */

typedef int mm_segment_t;
typedef void *fl_owner_t;

static inline int get_fs( void )
{
	return 0;
}

static inline void set_fs( int a UNUSED_ARG )
{}

#define KERNEL_DS (0)
#define kdevname( device ) ("")

extern int init_MUTEX( semaphore *sem );

extern struct file *filp_open(const char * filename, int flags, int mode);
extern int filp_close(struct file *filp, fl_owner_t id);

extern unsigned int nr_free_pagecache_pages( void );
extern unsigned int nr_free_pages( void );

#define BITS_PER_LONG (32)

struct completion {
	struct semaphore sem;
};

#define COMPLETION_INITIALIZER(work) { .sem = DECLARE_MUTEX_LOCKED(work) }

#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)

#define INIT_COMPLETION(x)	((x).sem = DECLARE_MUTEX_LOCKED(work))

void complete_and_exit( struct completion *, long ) __attribute__((noreturn));

void init_completion(struct completion *x);
void wait_for_completion(struct completion *x);
void complete(struct completion *x);


static inline void inode_add_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks += bytes >> 9;
	bytes &= 511;
	inode->i_bytes += bytes;
	if (inode->i_bytes >= 512) {
		inode->i_blocks++;
		inode->i_bytes -= 512;
	}
}

static inline void inode_sub_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks -= bytes >> 9;
	bytes &= 511;
	if (inode->i_bytes < bytes) {
		inode->i_blocks--;
		inode->i_bytes += 512;
	}
	inode->i_bytes -= bytes;
}

static inline loff_t inode_get_bytes(struct inode *inode)
{
	return (((loff_t)inode->i_blocks) << 9) + inode->i_bytes;
}

static inline void inode_set_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks = bytes >> 9;
	inode->i_bytes = bytes & 511;
}

typedef __u64 qsize_t;

/*
 * NO-OP when quota not configured.
 */
#define sb_dquot_ops				(NULL)
#define sb_quotactl_ops				(NULL)
#define DQUOT_INIT(inode)			do { } while(0)
#define DQUOT_DROP(inode)			do { } while(0)
#define DQUOT_ALLOC_INODE(inode)		(0)
#define DQUOT_FREE_INODE(inode)			do { } while(0)
#define DQUOT_SYNC(sb)				do { } while(0)
#define DQUOT_OFF(sb)				do { } while(0)
#define DQUOT_TRANSFER(inode, iattr)		(0)

static inline int DQUOT_PREALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	lock_kernel();
	inode_add_bytes(inode, (loff_t) nr);
	unlock_kernel();
	return 0;
}

static inline int DQUOT_PREALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_PREALLOC_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

static inline int DQUOT_ALLOC_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	lock_kernel();
	inode_add_bytes(inode, (loff_t)nr);
	unlock_kernel();
	return 0;
}

static inline int DQUOT_ALLOC_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_ALLOC_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
	return 0;
}

static inline void DQUOT_FREE_SPACE_NODIRTY(struct inode *inode, qsize_t nr)
{
	lock_kernel();
	inode_sub_bytes(inode, (loff_t)nr);
	unlock_kernel();
}

static inline void DQUOT_FREE_SPACE(struct inode *inode, qsize_t nr)
{
	DQUOT_FREE_SPACE_NODIRTY(inode, nr);
	mark_inode_dirty(inode);
}	

#define DQUOT_PREALLOC_BLOCK_NODIRTY(inode, nr)	DQUOT_PREALLOC_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_PREALLOC_BLOCK(inode, nr)	DQUOT_PREALLOC_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_ALLOC_BLOCK_NODIRTY(inode, nr) DQUOT_ALLOC_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_ALLOC_BLOCK(inode, nr) DQUOT_ALLOC_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_FREE_BLOCK_NODIRTY(inode, nr) DQUOT_FREE_SPACE_NODIRTY(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)
#define DQUOT_FREE_BLOCK(inode, nr) DQUOT_FREE_SPACE(inode, ((qsize_t)(nr)) << (inode)->i_sb->s_blocksize_bits)

extern void clear_inode(struct inode *);

#define remove_from_page_cache remove_inode_page

#define IS_RDONLY( something ) (0)

void fsync_super( struct super_block *s );

int test_clear_page_dirty(struct page *page);

static inline void clear_page_dirty(struct page *page)
{
	test_clear_page_dirty(page);
}

extern void generic_delete_inode(struct inode *inode);

#define in_interrupt() (0)
#define in_irq() (0)
#define might_sleep() ((void)(0))
#define PAGE_OFFSET (0)
#define current_is_pdflush() (0)

extern void __mark_inode_dirty(struct inode *inode, int flags);

struct mm_struct;
struct vm_operations_struct;

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* The address space we belong to. */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	unsigned long vm_flags;		/* Flags, listed below. */

	/*
	 * For areas with an address space and backing store,
	 * one of the address_space->i_mmap{,shared} lists,
	 * for shm areas, the list of attaches, otherwise unused.
	 */
	struct list_head shared;

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
	unsigned long vm_raend;		/* XXX: put full readahead info here. */
	void * vm_private_data;		/* was vm_pte (shared mem) */
};

/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs.
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int unused);
	int (*populate)(struct vm_area_struct * area, unsigned long address, unsigned long len, unsigned long prot, unsigned long pgoff, int nonblock);
};

#define BITS_PER_LONG (32)

extern void generic_delete_inode(struct inode *inode);
extern void generic_forget_inode(struct inode *inode);

#define called_for_sync() (0)
#define bdi_write_congested(bdi) (((void)(bdi)),(0))

#define get_seconds() (time(0))

extern struct page * filemap_nopage(struct vm_area_struct * area,
				    unsigned long address, int unused);

extern void __iget(struct inode * inode);

/* ioctl command encoding: 32 bits total, command in lower 16 bits,
 * size of the parameter structure in the lower 14 bits of the
 * upper 16 bits.
 * Encoding the size of the parameter structure in the ioctl request
 * is useful for catching programs compiled with old versions
 * and to avoid overwriting user space outside the user buffer area.
 * The highest 2 bits are reserved for indicating the ``access mode''.
 * NOTE: This limits the max parameter size to 16kB -1 !
 */

/*
 * The following is for compatibility across the various Linux
 * platforms.  The i386 ioctl numbering scheme doesn't really enforce
 * a type field.  De facto, however, the top 8 bits of the lower 16
 * bits are indeed used as a type field, so we might just as well make
 * this explicit here.  Please be sure to use the decoding macros
 * below from now on.
 */
#define _IOC_NRBITS	8
#define _IOC_TYPEBITS	8
#define _IOC_SIZEBITS	14
#define _IOC_DIRBITS	2

#define _IOC_NRMASK	((1 << _IOC_NRBITS)-1)
#define _IOC_TYPEMASK	((1 << _IOC_TYPEBITS)-1)
#define _IOC_SIZEMASK	((1 << _IOC_SIZEBITS)-1)
#define _IOC_DIRMASK	((1 << _IOC_DIRBITS)-1)

#define _IOC_NRSHIFT	0
#define _IOC_TYPESHIFT	(_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT	(_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT	(_IOC_SIZESHIFT+_IOC_SIZEBITS)

/*
 * Direction bits.
 */
#define _IOC_NONE	0U
#define _IOC_WRITE	1U
#define _IOC_READ	2U

#define _IOC(dir,type,nr,size) \
	(((dir)  << _IOC_DIRSHIFT) | \
	 ((type) << _IOC_TYPESHIFT) | \
	 ((nr)   << _IOC_NRSHIFT) | \
	 ((size) << _IOC_SIZESHIFT))

/* used to create numbers */
#define _IO(type,nr)		_IOC(_IOC_NONE,(type),(nr),0)
#define _IOR(type,nr,size)	_IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW(type,nr,size)	_IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size)	_IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))

/* used to decode ioctl numbers.. */
#define _IOC_DIR(nr)		(((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
#define _IOC_TYPE(nr)		(((nr) >> _IOC_TYPESHIFT) & _IOC_TYPEMASK)
#define _IOC_NR(nr)		(((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
#define _IOC_SIZE(nr)		(((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

/* ...and for the drivers/sound files... */

#define IOC_IN		(_IOC_WRITE << _IOC_DIRSHIFT)
#define IOC_OUT		(_IOC_READ << _IOC_DIRSHIFT)
#define IOC_INOUT	((_IOC_WRITE|_IOC_READ) << _IOC_DIRSHIFT)
#define IOCSIZE_MASK	(_IOC_SIZEMASK << _IOC_SIZESHIFT)
#define IOCSIZE_SHIFT	(_IOC_SIZESHIFT)

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
