/*
 * Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README
 */

/*
 * Debugging/logging/tracing/profiling/statistical facilities.
 */

#include "reiser4.h"

__u32 reiser4_current_trace_flags = 0;

extern void show_stack( unsigned long * esp );
extern void cond_resched( void );

static char panic_buf[ REISER4_PANIC_MSG_BUFFER_SIZE ];
static spinlock_t panic_guard = SPIN_LOCK_UNLOCKED;

/** Your best friend. Call it on each occasion.  This is called by
    fs/reiser4/debug.h:rpanic(). */
void reiser4_panic( const char *format /* format string */, ... /* rest */ )
{
	va_list args;

	/* FIXME-NIKITA bust_spinlocks() should go here. Quoting
	 * lib/bust_spinlocks.c:
	 *
	 * bust_spinlocks() clears any spinlocks which would prevent oops,
	 * die(), BUG() and panic() information from reaching the user.
	 */
	spin_lock( &panic_guard );
	va_start( args, format );
	vsprintf( panic_buf, format, args );
	va_end( args );
	spin_unlock( &panic_guard );
	
	/* print back-trace */
	show_stack( NULL );
	/* do something more impressive here, print content of
	   get_current_context() */
	panic( "reiser4 panicked cowardly: %s", panic_buf );
}

/**
 * Preemption point: this should be called periodically during long running
 * operations (carry, allocate, and squeeze are best examples)
 */
void preempt_point( void )
{
	cond_resched();
}

#if REISER4_DEBUG
/**
 * Debugging aid: return struct where information about locks taken by current
 * thread is accumulated. This can be used to formulate locking ordering
 * constraints and various assertions.
 *
 */
lock_counters_info *lock_counters()
{
	reiser4_context *ctx = get_current_context();
	assert ("jmacd-1123", ctx != NULL);
	return &ctx -> locks;
}

/**
 * check_stack() - check for possible stack overflow
 *
 */
void check_stack( void )
{
	char     dummy;
	unsigned gap;
	reiser4_context *context = get_current_context();

	if( context == NULL )
		return;
	gap = abs( &dummy - ( char * ) context );
	if( gap > REISER4_STACK_GAP ) {
		warning( "nikita-1079", "Stack overflow is close: %i", gap );
	}
	if( gap > REISER4_STACK_ABORT ) {
		rpanic( "nikita-1080", "Stack overflowed: %i", gap );
	}
	reiser4_stat_stack_check_max( gap );
}
#endif

#if REISER4_STATS
/**
 * Print statistical data accumulated so far.
 */
void reiser4_print_stats()
{
	reiser4_stat *s;
	int           i;

	s = &get_current_super_private() -> stats;
	info( "tree:" 
	      "\t cbk:\t %lli\n"
	      "\t cbk_found:\t %lli\n"
	      "\t cbk_notfound:\t %lli\n"
	      "\t cbk_restart:\t %lli\n"
	      "\t cbk_key_moved:\t %lli\n"
	      "\t cbk_met_ghost:\t %lli\n"
	      "\t cbk_cache_hit:\t %lli\n"
	      "\t cbk_cache_miss:\t %lli\n"
	      "\t cbk_cache_race:\t %lli\n"
	      "\t cbk_cache_utmost:\t %lli\n"
	      "\t pos_in_parent_hit:\t %lli\n"
	      "\t pos_in_parent_miss:\t %lli\n"
	      "\t pos_in_parent_set:\t %lli\n"
	      "\t fast_insert:\t %lli\n"
	      "\t fast_paste:\t %lli\n"
	      "\t fast_cut:\t %lli\n"
	      "\t reparenting:\t %lli\n"
	      "\t rd_key_skew:\t %lli\n"
	      "\t check_left_nonuniq:\t %lli\n"
	      "\t left_nonuniq_found:\t %lli\n"

	      "znode:\n"
	      "\t zload:\t %lli\n"
	      "\t zload_read:\t %lli\n"
	      "\t lock_znode:\t %lli\n"
	      "\t lock_znode_iteration:\t %lli\n"
	      "\t lock_neighbor:\t %lli\n"
	      "\t lock_neighbor_iteration:\t %lli\n"

	      "file:\n"
	      "\t wait_on_page:\t %lli\n"
	      "\t fsdata_alloc:\t %lli\n"
	      "\t private_data_alloc:\t %lli\n"
	      "\t writes:\t %lli\n"
	      "\t write repeats:\t %lli\n"
	      "\t tail2extent:\t %lli\n"
	      "\t extent2tail:\t %lli\n"

	      "flush:\n"
	      "\t squeeze:\t %lli\n"
	      "\t flush_carry:\t %lli\n"
	      "\t squeezed_completely:\t %lli\n"

	      "pool:\n"
	      "\t alloc:\t %lli\n"
	      "\t kmalloc:\t %lli\n"

	      "seal:\n"
	      "\t perfect_match:\t %lli\n"
	      "\t key_drift:\t %lli\n"
	      "\t out_of_cache:\t %lli\n"
	      "\t wrong_node:\t %lli\n"
	      "\t didnt_move:\t %lli\n"
	      "\t found:\t %lli\n"

	      "global:\n"
	      "\t non_uniq:\t %lli\n"
	      "\t non_uniq_max:\t %lli\n"
	      "\t stack_size_max:\t %lli\n"

	      "key:\n"
	      "\t eq0:\t %lli\n"
	      "\t eq1:\t %lli\n"
	      "\t eq2:\t %lli\n"
	      "\t eq3:\t %lli\n"

	      ,
	      
	      s -> tree.cbk,
	      s -> tree.cbk_found,
	      s -> tree.cbk_notfound,
	      s -> tree.cbk_restart,
	      s -> tree.cbk_key_moved,
	      s -> tree.cbk_met_ghost,
	      s -> tree.cbk_cache_hit,
	      s -> tree.cbk_cache_miss,
	      s -> tree.cbk_cache_race,
	      s -> tree.cbk_cache_utmost,
	      s -> tree.pos_in_parent_hit,
	      s -> tree.pos_in_parent_miss,
	      s -> tree.pos_in_parent_set,
	      s -> tree.fast_insert,
	      s -> tree.fast_paste,
	      s -> tree.fast_cut,
	      s -> tree.reparenting,
	      s -> tree.rd_key_skew,
	      s -> tree.check_left_nonuniq,
	      s -> tree.left_nonuniq_found,

	      s -> znode.zload,
	      s -> znode.zload_read,
	      s -> znode.lock_znode,
	      s -> znode.lock_znode_iteration,
	      s -> znode.lock_neighbor,
	      s -> znode.lock_neighbor_iteration,

	      s -> file.wait_on_page,
	      s -> file.fsdata_alloc,
	      s -> file.private_data_alloc,
	      s -> file.writes,
	      s -> file.write_repeats,
	      s -> file.tail2extent,
	      s -> file.extent2tail,

	      s -> flush.squeeze,
	      s -> flush.flush_carry,
	      s -> flush.squeezed_completely,

	      s -> pool.pool_alloc,
	      s -> pool.pool_kmalloc,

	      s -> seal.perfect_match,
	      s -> seal.key_drift,
	      s -> seal.out_of_cache,
	      s -> seal.wrong_node,
	      s -> seal.didnt_move,
	      s -> seal.found,

	      s -> non_uniq,
	      s -> non_uniq_max,
	      s -> stack_size_max,
	      
	      s -> key.eq0,
	      s -> key.eq1,
	      s -> key.eq2,
	      s -> key.eq3 );

	for( i = 0 ; i < REAL_MAX_ZTREE_HEIGHT ; ++ i ) {
		if( s -> level[ i ].total_hits_at_level <= 0 )
			continue;
		info( "tree: at level: %i\n"
		      "\t carry_restart:\t %lli\n"
		      "\t carry_done:\t %lli\n"
		      "\t carry_left_in_carry:\t %lli\n"
		      "\t carry_left_in_cache:\t %lli\n"
		      "\t carry_left_not_avail:\t %lli\n"
		      "\t carry_left_refuse:\t %lli\n"
		      "\t carry_right_in_carry:\t %lli\n"
		      "\t carry_right_in_cache:\t %lli\n"
		      "\t carry_right_not_avail:\t %lli\n"
		      "\t insert_looking_left:\t %lli\n"
		      "\t insert_looking_right:\t %lli\n"
		      "\t insert_alloc_new:\t %lli\n"
		      "\t insert_alloc_many:\t %lli\n"
		      "\t insert:\t %lli\n"
		      "\t delete:\t %lli\n"
		      "\t cut:\t %lli\n"
		      "\t paste:\t %lli\n"
		      "\t extent:\t %lli\n"
		      "\t paste_restarted:\t %lli\n"
		      "\t update:\t %lli\n"
		      "\t modify:\t %lli\n"
		      "\t half_split_race:\t %lli\n"
		      "\t dk_vs_create_race:\t %lli\n"
		      "\t track_lh:\t %lli\n"
		      "\t sibling_search:\t %lli\n",

		      i + LEAF_LEVEL,

		      s -> level[ i ].carry_restart,
		      s -> level[ i ].carry_done,
		      s -> level[ i ].carry_left_in_carry,
		      s -> level[ i ].carry_left_in_cache,
		      s -> level[ i ].carry_left_not_avail,
		      s -> level[ i ].carry_left_refuse,
		      s -> level[ i ].carry_right_in_carry,
		      s -> level[ i ].carry_right_in_cache,
		      s -> level[ i ].carry_right_not_avail,
		      s -> level[ i ].insert_looking_left,
		      s -> level[ i ].insert_looking_right,
		      s -> level[ i ].insert_alloc_new,
		      s -> level[ i ].insert_alloc_many,
		      s -> level[ i ].insert,
		      s -> level[ i ].delete,
		      s -> level[ i ].cut,
		      s -> level[ i ].paste,
		      s -> level[ i ].extent,
		      s -> level[ i ].paste_restarted,
		      s -> level[ i ].update,
		      s -> level[ i ].modify,
		      s -> level[ i ].half_split_race,
		      s -> level[ i ].dk_vs_create_race,
		      s -> level[ i ].track_lh,
		      s -> level[ i ].sibling_search );
	}
}
#else
void reiser4_print_stats()
{
}
#endif

/** 
 * tracing setup: global trace flags stored in global variable plus
 * per-thread trace flags plus per-fs trace flags.
 *
 */
__u32 get_current_trace_flags( void )
{
	return 
		get_current_context() -> trace_flags | 
		get_current_super_private() -> trace_flags |
		reiser4_current_trace_flags;
}

/** 
 * allocate memory. This calls kmalloc(), performs some additional checks, and
 * keeps track of how many memory was allocated on behalf of current super
 * block.
 */
void *reiser4_kmalloc( size_t size /* number of bytes to allocate */, 
		       int gfp_flag /* allocation flag */ )
{
	assert( "nikita-1407", get_current_super_private() != NULL );
	assert( "nikita-1408", lock_counters() -> spin_locked == 0 );

	ON_DEBUG( get_current_super_private() -> kmalloc_allocated += size );
	return kmalloc( size, gfp_flag );
}

/**
 * release memory allocated by reiser4_kmalloc() and update counter.
 *
 */
void  reiser4_kfree( void *area /* memory to from */, 
		     size_t size UNUSED_ARG /* number of bytes to free */ )
{
	assert( "nikita-1410", area != NULL );
	assert( "nikita-1411", get_current_super_private() != NULL );
	assert( "nikita-1412", 
		get_current_super_private() -> kmalloc_allocated >= ( int ) size );

	kfree( area );
	ON_DEBUG( get_current_super_private() -> kmalloc_allocated -= size );
}


#if REISER4_DEBUG

int no_counters_are_held()
{
	lock_counters_info *counters;

	counters = lock_counters();
	return
		( counters -> spin_locked_jnode      == 0 ) &&
		( counters -> spin_locked_tree       == 0 ) &&
		( counters -> spin_locked_dk         == 0 ) &&
		( counters -> spin_locked_txnh       == 0 ) &&
		( counters -> spin_locked_atom       == 0 ) &&
		( counters -> spin_locked_stack      == 0 ) &&
		( counters -> spin_locked_txnmgr     == 0 ) &&
		( counters -> spin_locked_inode      == 0 ) &&
		( counters -> spin_locked            == 0 ) &&
		( counters -> long_term_locked_znode == 0 );
}

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
