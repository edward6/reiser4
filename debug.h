/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Declarations of debug macros. */
  
#if !defined( __FS_REISER4_DEBUG_H__ )
#define __FS_REISER4_DEBUG_H__

#include "forward.h"
#include "reiser4.h"

#ifdef __KERNEL__
/* for __u?? types */
#include <linux/types.h>
/* for struct super_block, etc */
#include <linux/fs.h>
/* for in_interrupt() */
#include <asm/hardirq.h>
#endif

#include <linux/sched.h>

/* basic debug/logging output macro. "label" is unfamous "maintainer-id" */

/* generic function to produce formatted output, decorating it with
   whatever standard prefixes/postfixes we want. "Fun" is a function
   that will be actually called, can be printk, panic etc.
   This is for use by other debugging macros, not by users. */
#define DCALL(lev, fun, label, format, ...)					\
({										\
	reiser4_print_prefix(lev, label, __FUNCTION__, __FILE__, __LINE__);	\
	fun(lev format "\n" , ## __VA_ARGS__);					\
})

#define reiser4_panic(mid, format, ...)				\
	DCALL("", reiser4_do_panic, mid, format , ## __VA_ARGS__)

/* print message with indication of current process, file, line and
   function */
#define reiser4_log(label, format, ...) 				\
	DCALL(KERN_DEBUG, printk, label, format , ## __VA_ARGS__)
/* use info() for output without any kind of prefix like
    when doing output in several chunks. */
#define info(format, ...) printk(format , ## __VA_ARGS__)

/* Assertion checked during compilation. 
    If "cond" is false (0) we get duplicate case label in switch.
    Use this to check something like famous 
       cassert (sizeof(struct reiserfs_journal_commit) == 4096) ;
    in 3.x journal.c. If cassertion fails you get compiler error,
    so no "maintainer-id". 
    From post by Andy Chou <acc@CS.Stanford.EDU> at lkml. */
#define cassert(cond) ({ switch(-1) { case (cond): case 0: break; } })

#if defined(CONFIG_REISER4_DEBUG)
/* turn on assertions */
#define REISER4_DEBUG (1)
#else
#define REISER4_DEBUG (0)
#endif

#if defined(CONFIG_REISER4_CHECK_STACK)
/* check for stack overflow in each assertion check */
#define REISER4_DEBUG_STACK (1)
#else
#define REISER4_DEBUG_STACK (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_MODIFY)
/* this significantly slows down testing, but we should run our testsuite
   through with this every once in a while.  */
#define REISER4_DEBUG_MODIFY (1)
#else
#define REISER4_DEBUG_MODIFY (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_MEMCPY)
/* provide our own memcpy/memmove to profile shifts */
#define REISER4_DEBUG_MEMCPY (1)
#else
#define REISER4_DEBUG_MEMCPY (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_NODE)
/* check consistency of internal node structures */
#define REISER4_DEBUG_NODE (1)
#else
#define REISER4_DEBUG_NODE (0)
#endif

#if defined(CONFIG_REISER4_ZERO_NEW_NODE)
/* if this is non-zero, clear content of new node, otherwise leave whatever
   may happen to be here */
#define REISER4_ZERO_NEW_NODE (1)
#else
#define REISER4_ZERO_NEW_NODE (0)
#endif

#if defined(CONFIG_REISER4_TRACE)
/* tracing facility.
  
    REISER4_DEBUG doesn't necessary implies tracing, because tracing is only
    meaningful during debugging and can produce big amonts of output useless
    for average user.
*/
#define REISER4_TRACE (1)
#else
#define REISER4_TRACE (0)
#endif

#if defined(CONFIG_REISER4_EVENT_LOG)
/* collect tree traces */
#define REISER4_TRACE_TREE (1)
#else
#define REISER4_TRACE_TREE (0)
#endif

#if defined(CONFIG_REISER4_STATS)
/* collect internal stats. Should be switched to use kernel logging facility
   once latter merged.  */
#define REISER4_STATS (1)
#else
#define REISER4_STATS (0)
#endif

#if defined(CONFIG_REISER4_DEBUG_OUTPUT)
/* debugging print functions. */
#define REISER4_DEBUG_OUTPUT (1)
#else
#define REISER4_DEBUG_OUTPUT (0)
#endif

#if defined(CONFIG_PROFILING)
#define REISER4_LOCKPROF (1)
#else
#define REISER4_LOCKPROF (0)
#endif

#define noop   do {;} while(0)

#if REISER4_DEBUG
/* version of info that only actually prints anything when _d_ebugging
    is on */
#define dinfo( format, ... ) info( format , ## __VA_ARGS__ )
/* macro to catch logical errors. Put it into `default' clause of
    switch() statement. */
#define impossible( label, format, ... ) 			\
         reiser4_panic( label, "impossible: " format , ## __VA_ARGS__ )
/* assert assures that @cond is true. If it is not, reiser4_panic() is
   called. Use this for checking logical consistency and _never_ call
   this to check correctness of external data: disk blocks and user-input . */
#define assert(label, cond)						\
({									\
	check_stack();							\
	if(unlikely(!(cond)))						\
		reiser4_panic(label, "assertion failed: " #cond);	\
})

/* like assertion, but @expr is evaluated even if REISER4_DEBUG is off. */
#define check_me( label, expr )	assert( label, ( expr ) )

#define ON_DEBUG( exp ) exp

typedef struct lock_counters_info {
	int rw_locked_tree;
	int read_locked_tree;
	int write_locked_tree;

	int spin_locked_jnode;
	int spin_locked_dk;
	int spin_locked_txnh;
	int spin_locked_atom;
	int spin_locked_stack;
	int spin_locked_txnmgr;
	int spin_locked_ktxnmgrd;
	int spin_locked_fq;
	int spin_locked_super;
	int spin_locked_inode_object;
	int spin_locked;
	int long_term_locked_znode;

	int inode_sem_r;
	int inode_sem_w;

	int d_refs;
	int x_refs;
	int t_refs;
} lock_counters_info;

extern lock_counters_info *lock_counters(void);

extern void schedulable (void); 

#else

#define dinfo( format, args... ) noop
#define impossible( label, format, args... ) noop
#define assert( label, cond ) noop
#define check_me( label, expr )	( ( void ) ( expr ) )
#define ON_DEBUG( exp )
#define schedulable() might_sleep()

/* REISER4_DEBUG */
#endif

/* flags controlling debugging behavior. Are set through debug_flags=N mount
   option. */
typedef enum {
	/* print a lot of information during panic. */
	REISER4_VERBOSE_PANIC = 0x00000001,
	/* print a lot of information during umount */
	REISER4_VERBOSE_UMOUNT = 0x00000002,
	/* print gathered statistics on umount */
	REISER4_STATS_ON_UMOUNT = 0x00000004,
	/* check node consistency */
	REISER4_CHECK_NODE = 0x00000008,
        /* print gathered statistics on statfs() call */
	REISER4_STATS_ON_STATFS = 0x10
} reiser4_debug_flags;

extern int reiser4_are_all_debugged(struct super_block *super, __u32 flags);
extern int reiser4_is_debugged(struct super_block *super, __u32 flag);

extern int is_in_reiser4_context(void);

#define ON_CONTEXT(e)	do {			\
	if(is_in_reiser4_context()) {		\
		e;				\
	} } while(0)

#define ON_DEBUG_CONTEXT( e ) ON_DEBUG( ON_CONTEXT( e ) )

#if REISER4_DEBUG_MODIFY
#define ON_DEBUG_MODIFY( exp ) exp
#else
#define ON_DEBUG_MODIFY( exp )
#endif

#define wrong_return_value( label, function )				\
	impossible( label, "wrong return value from " function )
#define warning( label, format, ... )					\
	DCALL( KERN_WARNING, printk, label, "WARNING: " format , ## __VA_ARGS__ )
#define not_yet( label, format, ... )				\
	reiser4_panic( label, "NOT YET IMPLEMENTED: " format , ## __VA_ARGS__ )

#if REISER4_TRACE
/* helper macro for tracing, see trace_stamp() below. */
#define trace_if( flags, e ) 							\
	if( get_current_trace_flags() & (flags) ) e
#else
#define trace_if( flags, e ) noop
#endif

/* tracing flags. */
typedef enum {
	/* trace nothing */
	NO_TRACE = 0,
	/* trace vfs interaction functions from vfs_ops.c */
	TRACE_VFS_OPS = (1 << 0),	/* 0x00000001 */
	/* trace plugin handling functions */
	TRACE_PLUGINS = (1 << 1),	/* 0x00000002 */
	/* trace tree traversals */
	TRACE_TREE = (1 << 2),	/* 0x00000004 */
	/* trace znode manipulation functions */
	TRACE_ZNODES = (1 << 3),	/* 0x00000008 */
	/* trace node layout functions */
	TRACE_NODES = (1 << 4),	/* 0x00000010 */
	/* trace directory functions */
	TRACE_DIR = (1 << 5),	/* 0x00000020 */
	/* trace flush code verbosely */
	TRACE_FLUSH_VERB = (1 << 6),	/* 0x00000040 */
	/* trace flush code */
	TRACE_FLUSH = (1 << 7),	/* 0x00000080 */
	/* trace carry */
	TRACE_CARRY = (1 << 8),	/* 0x00000100 */
	/* trace how tree (web) of znodes if maintained through tree
	   balancings. */
	TRACE_ZWEB = (1 << 9),	/* 0x00000200 */
	/* trace transactions. */
	TRACE_TXN = (1 << 10),	/* 0x00000400 */
	/* trace object id allocation/releasing */
	TRACE_OIDS = (1 << 11),	/* 0x00000800 */
	/* trace item shifts */
	TRACE_SHIFT = (1 << 12),	/* 0x00001000 */
	/* trace page cache */
	TRACE_PCACHE = (1 << 13),	/* 0x00002000 */
	/* trace extents */
	TRACE_EXTENTS = (1 << 14),	/* 0x00004000 */
	/* trace locks */
	TRACE_LOCKS = (1 << 15),	/* 0x00008000 */
	/* trace coords */
	TRACE_COORDS = (1 << 16),	/* 0x00010000 */
	/* trace read-IO functions */
	TRACE_IO_R = (1 << 17),	/* 0x00020000 */
	/* trace write-IO functions */
	TRACE_IO_W = (1 << 18),	/* 0x00040000 */

	/* trace log writing */
	TRACE_LOG = (1 << 19),	/* 0x00080000 */

	/* trace journal replaying */
	TRACE_REPLAY = (1 << 20),	/* 0x00100000 */

	/* trace space allocation */
	TRACE_ALLOC = (1 << 21),	/* 0x00200000 */

	/* trace space reservation */
	TRACE_RESERVE2 = (1 << 22),	/* 0x00400000 */

	/* trace emergency flush */
	TRACE_EFLUSH  = (1 << 23),	/* 0x00800000 */

	/* vague section: used to trace bugs. Use it to issue optional prints
	   at arbitrary points of code. */
	TRACE_BUG = (1 << 31),	/* 0x80000000 */

	/* trace everything above */
	TRACE_ALL = 0xffffffffu
} reiser4_trace_flags;

extern __u32 reiser4_current_trace_flags;

/* just print where we are: file, function, line */
#define trace_stamp( f )   trace_if( f, reiser4_log( "trace", "" ) )
/* print value of "var" */
#define trace_var( f, format, var ) 				\
        trace_if( f, reiser4_log( "trace", #var ": " format, var ) )
/* print output only if appropriate trace flag(s) is on */
#define trace_on( f, ... )   trace_if( f, info( __VA_ARGS__ ) )

/* profiling. This is i386, rdtsc-based profiling. */
#if (defined(__i386__) || defined(CONFIG_USERMODE)) && defined(CONFIG_REISER4_PROF)
#define REISER4_PROF (1)
#else
#define REISER4_PROF (0)
#endif

#if REISER4_PROF
/* include from asm-i386 directly to work under UML/i386 */
#include <asm-i386/msr.h>

#define REISER4_PROF_TRACE_NUM (4)
#define REISER4_PROF_TRACE_DEPTH (6)

typedef struct reiser4_trace {
	unsigned long hash;
	void *trace[REISER4_PROF_TRACE_DEPTH];
	__u64 hits;
} reiser4_trace;

typedef struct reiser4_prof_cnt {
	__u64 nr;
	__u64 total;
	__u64 max;
	__u64 noswtch_nr;
	__u64 noswtch_total;
	__u64 noswtch_max;
	reiser4_trace bt[REISER4_PROF_TRACE_NUM];
} reiser4_prof_cnt;

typedef struct reiser4_prof {
	reiser4_prof_cnt jload;
	reiser4_prof_cnt carry;
	reiser4_prof_cnt flush_alloc;
	reiser4_prof_cnt forward_squalloc;
	reiser4_prof_cnt load_page;
} reiser4_prof;

extern unsigned long nr_context_switches(void);
extern void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now, 
			    unsigned long swtch_mark);

#define PROF_BEGIN(aname)							\
	unsigned long __swtch_mark__ ## aname = nr_context_switches();		\
	__u64 __prof_cnt__ ## aname = ({ __u64 __tmp_prof ;			\
			      		rdtscll(__tmp_prof) ; __tmp_prof; })

#define PROF_END(aname, acnt)							\
({										\
	__u64 __prof_end;							\
	reiser4_prof *prf;							\
										\
	rdtscll(__prof_end);							\
	prf = &get_super_private_nocheck(reiser4_get_current_sb()) -> prof;	\
	update_prof_cnt(&prf->acnt, __prof_cnt__ ## aname, __prof_end,		\
			__swtch_mark__ ## aname);				\
})

extern void calibrate_prof(void);

#else

typedef struct reiser4_prof_cnt {} reiser4_prof_cnt;
typedef struct reiser4_prof {} reiser4_prof;

#define PROF_BEGIN(aname) noop
#define PROF_END(aname, acnt) noop
#define calibrate_prof() noop

#endif

#if REISER4_STATS

/* following macros update counters from &reiser4_stat below, which
   see */

#define ON_STATS(e) e
#define STS (get_super_private_nocheck(reiser4_get_current_sb()) -> stats)

/* Macros to gather statistical data. If REISER4_STATS is disabled, they
   are preprocessed to nothing.
  
   reiser4_stat_foo_add( counter ) increases by one counter in foo section of 
   &reiser4_stat - big struct used to collect all statistical data.
  
*/

#define	reiser4_stat_inc_at(sb, counter)        		\
	(++ (get_super_private_nocheck(sb) -> stats . counter))
#define	reiser4_stat_inc(counter)        (++ STS . counter)
#define reiser4_stat_add(counter, delta) (STS . counter += (delta))

#define	reiser4_stat_inc_at_level(lev, stat)					\
({										\
	int level;								\
										\
	level = (lev) - LEAF_LEVEL;						\
	if (level >= 0) {							\
		if(level < REAL_MAX_ZTREE_HEIGHT) {				\
			reiser4_stat_inc(level[level]. stat);			\
			reiser4_stat_inc(level[level]. total_hits_at_level);	\
		}								\
	}									\
})

#define	reiser4_stat_add_at_level_value(lev, stat, value)				\
({									\
	int level;							\
									\
	level = (lev) - LEAF_LEVEL;					\
	if (level >= 0) {						\
		if(level < REAL_MAX_ZTREE_HEIGHT) {			\
			reiser4_stat_add(level[level]. stat , value );	\
			reiser4_stat_inc(level[level]. total_hits_at_level);	\
		}							\
	}								\
})

#define	reiser4_stat_level_inc(l, stat)			\
	reiser4_stat_inc_at_level((l)->level_no, stat)

#define MAX_CNT(field, value)						\
({									\
	if(get_super_private_nocheck(reiser4_get_current_sb()) &&	\
	    (value) > STS.field)					\
		STS.field = (value);					\
})

#define reiser4_stat_nuniq_max(gen)		\
({						\
	reiser4_stat_inc(non_uniq);		\
	MAX_CNT(non_uniq_max, gen);		\
})

#define reiser4_stat_stack_check_max( gap ) MAX_CNT( stack_size_max, gap )

/* statistics gathering features. */

/* type of statistics counters */
typedef unsigned long stat_cnt;

typedef struct reiser4_level_statistics {
	/* carries restarted due to deadlock avoidance algorithm */
	stat_cnt carry_restart;
	/* carries performed */
	stat_cnt carry_done;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a carry set. */
	stat_cnt carry_left_in_carry;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it already in a memory. */
	stat_cnt carry_left_in_cache;
	/* how many times carry, trying to find left neighbor of a given node,
	   found it is not in a memory. */
	stat_cnt carry_left_missed;
	/* how many times carry, trying to find left neighbor of a given node,
	   found that left neighbor either doesn't exist (we are at the left
	   border of the tree already), or that there is extent on the left.
	*/
	stat_cnt carry_left_not_avail;
	/* how many times carry, trying to find left neighbor of a given node,
	   gave this up to avoid deadlock */
	stat_cnt carry_left_refuse;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a carry set. */
	stat_cnt carry_right_in_carry;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it already in a memory. */
	stat_cnt carry_right_in_cache;
	/* how many times carry, trying to find right neighbor of a given
	   node, found it is not in a memory. */
	stat_cnt carry_right_missed;
	/* how many times carry, trying to find right neighbor of a given
	   node, found that right neighbor either doesn't exist (we are at the
	   right border of the tree already), or that there is extent on the
	   right.
	*/
	stat_cnt carry_right_not_avail;
	/* how many times insertion has to look into the left neighbor,
	   searching for the free space. */
	stat_cnt insert_looking_left;
	/* how many times insertion has to look into the right neighbor,
	   searching for the free space. */
	stat_cnt insert_looking_right;
	/* how many times insertion has to allocate new node, searching for
	   the free space. */
	stat_cnt insert_alloc_new;
	/* how many times insertion has to allocate several new nodes in a
	   row, searching for the free space. */
	stat_cnt insert_alloc_many;
	/* how many insertions were performed by carry. */
	stat_cnt insert;
	/* how many deletions were performed by carry. */
	stat_cnt delete;
	/* how many cuts were performed by carry. */
	stat_cnt cut;
	/* how many pastes (insertions into existing items) were performed by
	   carry. */
	stat_cnt paste;
	/* how many extent insertions were done by carry. */
	stat_cnt extent;
	/* how many paste operations were restarted as insert. */
	stat_cnt paste_restarted;
	/* how many updates of delimiting keys were performed by carry. */
	stat_cnt update;
	/* how many times carry notified parent node about updates in its
	   child. */
	stat_cnt modify;
	/* how many times node was found reparented at the time when its
	   parent has to be updated. */
	stat_cnt half_split_race;
	/* how many times new node was inserted into sibling list after
	   concurrent balancing modified right delimiting key if its left
	   neighbor.
	*/
	stat_cnt dk_vs_create_race;
	/* how many times insert or paste ultimately went into node different
	   from original target */
	stat_cnt track_lh;
	/* how many times sibling lookup required getting that high in a
	   tree */
	stat_cnt sibling_search;
	/* key was moved out of node while thread was waiting for the lock */
	stat_cnt cbk_key_moved;
	/* node was moved out of tree while thread was waiting for the lock */
	stat_cnt cbk_met_ghost;
	/* for how many pages on this level ->releasepage() was called. */
	stat_cnt page_try_release;
	/* how many pages were released on this level */
	stat_cnt page_released;
	/* how many times emergency flush was invoked on this level */
	stat_cnt emergency_flush;
	/* how many requests for znode long term lock couldn't succeed
	 * immediately. */
	stat_cnt long_term_lock_contented;
	/* how many requests for znode long term lock managed to succeed
	 * immediately. */
	stat_cnt long_term_lock_uncontented;
	struct {
		/* calls to jload() */
		stat_cnt jload;
		/* calls to jload() that found jnode already loaded */
		stat_cnt jload_already;
		/* calls to jload() that found page already in memory */
		stat_cnt jload_page;
		/* calls to jload() that found jnode with asynchronous io
		 * started */
		stat_cnt jload_async;
		/* calls to jload() that actually had to read data */
		stat_cnt jload_read;
	} jnode;
	struct {
		/* calls to lock_znode() */
		stat_cnt lock_znode;
		/* number of times loop inside lock_znode() was executed */
		stat_cnt lock_znode_iteration;
		/* calls to lock_neighbor() */
		stat_cnt lock_neighbor;
		/* number of times loop inside lock_neighbor() was executed */
		stat_cnt lock_neighbor_iteration;
	} znode;
	stat_cnt total_hits_at_level;
	stat_cnt time_slept;
} reiser4_level_stat;

/* set of statistics counter. This is embedded into super-block when
   REISER4_STATS is on. */
typedef struct reiser4_statistics {
	struct {
		/* calls to coord_by_key */
		stat_cnt cbk;
		/* calls to coord_by_key that found requested key */
		stat_cnt cbk_found;
		/* calls to coord_by_key that didn't find requested key */
		stat_cnt cbk_notfound;
		/* number of times calls to coord_by_key restarted */
		stat_cnt cbk_restart;
		/* calls to coord_by_key that found key in coord cache */
		stat_cnt cbk_cache_hit;
		/* calls to coord_by_key that didn't find key in coord
		   cache */
		stat_cnt cbk_cache_miss;
		/* cbk cache search found wrong node */
		stat_cnt cbk_cache_wrong_node;
		/* search for key in coord cache raced against parallel
		   balancing and lose. This should be rare. If not,
		   update cbk_cache_search() according to comment
		   therewithin.
		*/
		stat_cnt cbk_cache_race;
		/* number of times coord of child in its parent, cached
		   in a former, was reused. */
		stat_cnt pos_in_parent_hit;
		/* number of time binary search for child position in
		   its parent had to be redone. */
		stat_cnt pos_in_parent_miss;
		/* number of times position of child in its parent was
		   cached in the former */
		stat_cnt pos_in_parent_set;
		/* how many times carry() was skipped by doing "fast
		   insertion path". See
		   fs/reiser4/plugin/node/node.h:->fast_insert() method.
		*/
		stat_cnt fast_insert;
		/* how many times carry() was skipped by doing "fast
		   paste path". See
		   fs/reiser4/plugin/node/node.h:->fast_paste() method.
		*/
		stat_cnt fast_paste;
		/* how many times carry() was skipped by doing "fast
		   cut path". See
		   fs/reiser4/plugin/node/node.h:->cut_insert() method.
		*/
		stat_cnt fast_cut;
		/* children reparented due to shifts at the parent level */
		stat_cnt reparenting;
		/* right delimiting key is not exact */
		stat_cnt rd_key_skew;
		/* how many times lookup_multikey() has to restart from the
		   beginning because of the broken seal. */
		stat_cnt multikey_restart;
		stat_cnt check_left_nonuniq;
		stat_cnt left_nonuniq_found;
	} tree;
	reiser4_level_stat level[REAL_MAX_ZTREE_HEIGHT];
	struct {
		stat_cnt lookup;
		stat_cnt create;
		stat_cnt mkdir;
		stat_cnt symlink;
		stat_cnt mknod;
		stat_cnt rename;
		stat_cnt readlink;
		stat_cnt follow_link;
		stat_cnt setattr;
		stat_cnt getattr;
		stat_cnt read;
		stat_cnt write;
		stat_cnt truncate;
		stat_cnt statfs;
		stat_cnt bmap;
		stat_cnt link;
		stat_cnt llseek;
		stat_cnt readdir;
		stat_cnt ioctl;
		stat_cnt mmap;
		stat_cnt unlink;
		stat_cnt rmdir;
		stat_cnt alloc_inode;
		stat_cnt destroy_inode;
		stat_cnt drop_inode;
		stat_cnt delete_inode;
		stat_cnt write_super;
	} vfs_calls;
	struct {
		struct {
			stat_cnt calls;
			stat_cnt reset;
			stat_cnt rewind_left;
			stat_cnt left_non_uniq;
			stat_cnt left_restart;
			stat_cnt rewind_right;
			stat_cnt adjust_pos;
			stat_cnt adjust_lt;
			stat_cnt adjust_gt;
			stat_cnt adjust_eq;
		} readdir;
	} dir;
	/* statistics of unix file plugin */
	struct {
		stat_cnt wait_on_page;
		stat_cnt fsdata_alloc;
		stat_cnt private_data_alloc;

		/* number of tail conversions */
		stat_cnt tail2extent;
		stat_cnt extent2tail;

		/* find_next_item statistic */
		stat_cnt find_next_item;
		stat_cnt find_next_item_via_seal;
		stat_cnt find_next_item_via_right_neighbor;
		stat_cnt find_next_item_via_cbk;
	} file;
	struct {
		/* how many unformatted nodes were read */
		stat_cnt unfm_block_reads;

		/* extent_write seals and unlock znode before locking/capturing
		   page which is to be modified. After page is locked/captured
		   it validates a seal. Number of found broken seals is stored
		   here
		*/
		stat_cnt broken_seals;

		/* extent_write calls balance_dirty_pages after it modifies every page. Before that it seals node it
		   currently holds and uses seal_validate to lock it again. This field stores how many times
		   balance_dirty_pages broke that seal and caused to repease search tree traversal
		*/
		stat_cnt bdp_caused_repeats;
	} extent;
	struct { /* stats on tail items */		
		/* tail_write calls balance_dirty_pages after every call to insert_flow. Before that it seals node it
		   currently holds and uses seal_validate to lock it again. This field stores how many times
		   balance_dirty_pages broke that seal and caused to repease search tree traversal
		*/
		stat_cnt bdp_caused_repeats;
	} tail;
	struct {
		/* jiffies, spent in atom_wait_event() */
		stat_cnt slept_in_wait_event;
		/* jiffies, spent in capture_fuse_wait (wait for atom state change) */
		stat_cnt slept_in_wait_atom;
		/* number of commits */
		stat_cnt commits;
		/*number of post commit writes */
		stat_cnt post_commit_writes;
		/* jiffies, spent in commits and post commit writes */
		stat_cnt time_spent_in_commits;
	} txnmgr;
	struct {
		/* how many nodes were squeezed to left neighbor completely */
		stat_cnt squeezed_completely;
		/* how many times nodes with unallocated children are written */
		stat_cnt flushed_with_unallocated;
		/* how many leaves were squeezed to left */
		stat_cnt squeezed_leaves;
		/* how many items were squeezed on leaf level */
		stat_cnt squeezed_leaf_items;
		/* how mnay bytes were squeezed on leaf level */
		stat_cnt squeezed_leaf_bytes;
		/* how many times jnode_flush was called */
		stat_cnt flush;
		/* how many nodes were scanned by scan_left() */
		stat_cnt left;
		/* how many nodes were scanned by scan_right() */
		stat_cnt right;
		/* an overhead of MTFLUSH semaphore */
		stat_cnt slept_in_mtflush_sem;
	} flush;
	struct {
		/* how many carry objects were allocated */
		stat_cnt alloc;
		/* how many "extra" carry objects were allocated by
		   kmalloc. */
		stat_cnt kmalloc;
	} pool;
	struct {
		/* seals that were found pristine */
		stat_cnt perfect_match;
		/* how many times key drifted from sealed node */
		stat_cnt key_drift;
		/* how many times node under seal was out of cache */
		stat_cnt out_of_cache;
		/* how many times wrong node was found under seal */
		stat_cnt wrong_node;
		/* how many times coord was found in exactly the same position
		   under seal */
		stat_cnt didnt_move;
		/* how many times key was actually found under seal */
		stat_cnt found;
	} seal;
	/* how many non-unique keys were scanned into tree */
	stat_cnt non_uniq;
	/* maximal length of sequence of items with identical keys found
	   in a tree */
	stat_cnt non_uniq_max;
	/* maximal stack size ever consumed by reiser4 thread. */
	stat_cnt stack_size_max;
} reiser4_stat;

struct kobject;
extern int reiser4_populate_kattr_level_dir(struct kobject * kobj);

#else

#define ON_STATS(e) noop

#define	reiser4_stat_inc(counter)  noop
#define reiser4_stat_add(counter, delta) noop

#define	reiser4_stat_inc_at(sb, counter) noop
#define	reiser4_stat_inc_at_level(lev, stat) noop
#define reiser4_stat_add_at_level_value(lev, stat, cnt) noop
#define	reiser4_stat_level_inc(l, stat) noop
#define reiser4_stat_nuniq_max(gen) noop
#define reiser4_stat_stack_check_max(gap) noop

typedef struct {
} reiser4_stat;

#define reiser4_populate_kattr_level_dir(kobj, i) (0)

#endif

extern int reiser4_populate_kattr_dir(struct kobject * kobj);

extern void reiser4_do_panic(const char *format, ...)
__attribute__ ((noreturn, format(printf, 1, 2)));

extern void reiser4_print_prefix(const char *level, const char *mid,
				 const char *function, 
				 const char *file, int lineno);

extern int preempt_point(void);
extern void reiser4_print_stats(void);

extern void *reiser4_kmalloc(size_t size, int gfp_flag);
extern void reiser4_kfree(void *area, size_t size);
extern void reiser4_kfree_in_sb(void *area, size_t size, struct super_block *sb);
extern __u32 get_current_trace_flags(void);

#if REISER4_DEBUG
extern int no_counters_are_held(void);
extern void check_stack(void);
#endif

#if REISER4_DEBUG_OUTPUT && REISER4_DEBUG
extern void print_lock_counters(const char *prefix, const lock_counters_info * info);
#else
#define print_lock_counters( p, i ) noop
#endif

#define REISER4_STACK_ABORT          (8192 - sizeof(struct task_struct) - 30)
#define REISER4_STACK_GAP            (REISER4_STACK_ABORT - 100)

#if REISER4_DEBUG_MEMCPY
extern void *xmemcpy(void *dest, const void *src, size_t n);
extern void *xmemmove(void *dest, const void *src, size_t n);
extern void *xmemset(void *s, int c, size_t n);
#else
#define xmemcpy( d, s, n ) memcpy( ( d ), ( s ), ( n ) )
#define xmemmove( d, s, n ) memmove( ( d ), ( s ), ( n ) )
#define xmemset( s, c, n ) memset( ( s ), ( c ), ( n ) )
#endif

/* true if @i is power-of-two. Useful for rate-limited warnings, etc. */
#define IS_POW(i) 				\
({						\
	typeof(i) __i;				\
						\
	__i = (i);				\
	!(__i & (__i - 1));			\
})

/* __FS_REISER4_DEBUG_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
