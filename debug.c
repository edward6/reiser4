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
	dump_stack();

	/* do something more impressive here, print content of
	   get_current_context() */
	if( get_current_context() != NULL ) {
		struct super_block *super;

		print_lock_counters( "pins held", lock_counters() );
		show_context( 0 );
		super = get_current_context() -> super;
		if( ( get_super_private( super ) != NULL ) &&
		    reiser4_is_debugged( super, REISER4_VERBOSE_PANIC ) )
			print_znodes( "znodes", current_tree );
	}
	panic( "reiser4 panicked cowardly: %s", panic_buf );
}

/**
 * Preemption point: this should be called periodically during long running
 * operations (carry, allocate, and squeeze are best examples)
 */
int preempt_point( void )
{
	cond_resched();
	return signal_pending( current );
}

/**
 * conditionally call preempt_point() when debugging is on. Used to test MT
 * stuff with better thread interfusion.
 */
void check_preempt( void )
{
	if( REISER4_DEBUG == 3 )
		preempt_point();
}

#if REISER4_DEBUG
/**
 * Debugging aid: return struct where information about locks taken by current
 * thread is accumulated. This can be used to formulate lock ordering
 * constraints and various assertions.
 *
 */
lock_counters_info *lock_counters()
{
	reiser4_context *ctx = get_current_context();
	assert ("jmacd-1123", ctx != NULL);
	return &ctx -> locks;
}

void print_lock_counters( const char *prefix, lock_counters_info *info )
{
	info( "%s: jnode: %i, tree: %i, dk: %i, txnh: %i, atom: %i, stack: %i, txnmgr: %i "
	      "inode: %i, spin: %i, page: %i, long: %i\n"
	      "d: %i, x: %i, t: %i\n", prefix,
	      info -> spin_locked_jnode,
	      info -> spin_locked_tree,
	      info -> spin_locked_dk,
	      info -> spin_locked_txnh,
	      info -> spin_locked_atom,
	      info -> spin_locked_stack,
	      info -> spin_locked_txnmgr,
	      info -> spin_locked_inode,
	      info -> spin_locked,
	      info -> page_locked,
	      info -> long_term_locked_znode,
	      
	      info -> d_refs,
	      info -> x_refs,
	      info -> t_refs );
}

/**
 * check_stack() - check for possible stack overflow
 *
 */
void check_stack( void )
{
	if (REISER4_DEBUG > 1) {
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
}

int reiser4_is_debugged( struct super_block *super, __u32 flag )
{
	return get_super_private( super ) -> debug_flags & flag;
}

int reiser4_are_all_debugged( struct super_block *super, __u32 flags )
{
	return ( get_super_private( super ) -> debug_flags & flags ) == flags;
}

#endif

#if REISER4_STATS
/**
 * Print statistical data accumulated so far.
 */
void reiser4_print_stats()
{
	reiser4_stat *s;
	reiser4_tree *t;
	int           i;

	s = &get_current_super_private() -> stats;
	info( "tree:" 
	      "\t cbk:\t %lu\n"
	      "\t cbk_found:\t %lu\n"
	      "\t cbk_notfound:\t %lu\n"
	      "\t cbk_restart:\t %lu\n"
	      "\t cbk_cache_hit:\t %lu\n"
	      "\t cbk_cache_miss:\t %lu\n"
	      "\t cbk_cache_wrong_node:\t %lu\n"
	      "\t cbk_cache_race:\t %lu\n"
	      "\t cbk_cache_utmost:\t %lu\n"
	      "\t pos_in_parent_hit:\t %lu\n"
	      "\t pos_in_parent_miss:\t %lu\n"
	      "\t pos_in_parent_set:\t %lu\n"
	      "\t fast_insert:\t %lu\n"
	      "\t fast_paste:\t %lu\n"
	      "\t fast_cut:\t %lu\n"
	      "\t reparenting:\t %lu\n"
	      "\t rd_key_skew:\t %lu\n"
	      "\t multikey_restart:\t %lu\n"
	      "\t check_left_nonuniq:\t %lu\n"
	      "\t left_nonuniq_found:\t %lu\n",
	      s -> tree.cbk,
	      s -> tree.cbk_found,
	      s -> tree.cbk_notfound,
	      s -> tree.cbk_restart,
	      s -> tree.cbk_cache_hit,
	      s -> tree.cbk_cache_miss,
	      s -> tree.cbk_cache_wrong_node,
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
	      s -> tree.multikey_restart,
	      s -> tree.check_left_nonuniq,
	      s -> tree.left_nonuniq_found );

	info( "znode:\n"
	      "\t zload:\t %lu\n"
	      "\t zload_read:\t %lu\n"
	      "\t lock_znode:\t %lu\n"
	      "\t lock_znode_iteration:\t %lu\n"
	      "\t lock_neighbor:\t %lu\n"
	      "\t lock_neighbor_iteration:\t %lu\n",
	      s -> znode.zload,
	      s -> znode.zload_read,
	      s -> znode.lock_znode,
	      s -> znode.lock_znode_iteration,
	      s -> znode.lock_neighbor,
	      s -> znode.lock_neighbor_iteration );
	      
	info( "dir:\n"
	      "\treaddir_calls:\t %lu\n"
	      "\treaddir_reset:\t %lu\n"
	      "\treaddir_rewind_left:\t %lu\n"
	      "\treaddir_left_non_uniq:\t %lu\n"
	      "\treaddir_left_restart:\t %lu\n"
	      "\treaddir_rewind_right:\t %lu\n"
	      "\treaddir_adjust_pos:\t %lu\n"
	      "\treaddir_adjust_lt:\t %lu\n"
	      "\treaddir_adjust_gt:\t %lu\n"
	      "\treaddir_adjust_eq:\t %lu\n",

	      s -> dir.readdir.calls,
	      s -> dir.readdir.reset,
	      s -> dir.readdir.rewind_left,
	      s -> dir.readdir.left_non_uniq,
	      s -> dir.readdir.left_restart,
	      s -> dir.readdir.rewind_right,
	      s -> dir.readdir.adjust_pos,
	      s -> dir.readdir.adjust_lt,
	      s -> dir.readdir.adjust_gt,
	      s -> dir.readdir.adjust_eq );

	info( "file:\n"
	      "\t wait_on_page:\t %lu\n"
	      "\t fsdata_alloc:\t %lu\n"
	      "\t private_data_alloc:\t %lu\n"
	      "\t writes:\t %lu\n"
	      "\t write repeats:\t %lu\n"
	      "\t tail2extent:\t %lu\n"
	      "\t extent2tail:\t %lu\n"
	      "\t unformatted node pointers added (hole plugging not included):\t %lu\n"
	      "\t find_items:\t %lu\n"
	      "\t full find items:\t%lu\n",
	      s -> file.wait_on_page,
	      s -> file.fsdata_alloc,
	      s -> file.private_data_alloc,
	      s -> file.writes,
	      s -> file.write_repeats,
	      s -> file.tail2extent,
	      s -> file.extent2tail,
	      s -> file.pointers,
	      s -> file.find_items,
	      s -> file.full_find_items );
	info( "extent:\n"
	      "\t read unformatted nodes:\t %lu\n"
	      "\t broken seals:\t %lu\n",
	      s -> extent.unfm_block_reads,
	      s -> extent.broken_seals );

	info( "flush:\n"
	      "\t squeeze:\t %lu\n"
	      "\t flush_carry:\t %lu\n"
	      "\t squeezed_completely:\t %lu\n"
	      "\t flushed with unallocated children: \t %lu\n"
	      "\t XXXX leaves squeezed to left:\t %lu\n"
	      "\t XXXX items squeezed in those leaves:\t %lu\n"
	      "\t XXXX bytes in those items:\t %lu\n",
	      s -> flush.squeeze,
	      s -> flush.flush_carry,
	      s -> flush.squeezed_completely,
	      s -> flush.flushed_with_unallocated,
	      /*
	       * FIXME-VS: urgently added leaf squeeze stats
	       */
	      s -> flush.squeezed_leaves,
	      s -> flush.squeezed_leaf_items,
	      s -> flush.squeezed_leaf_bytes );


	info( "pool:\n"
	      "\t alloc:\t %lu\n"
	      "\t kmalloc:\t %lu\n"

	      "seal:\n"
	      "\t perfect_match:\t %lu\n"
	      "\t key_drift:\t %lu\n"
	      "\t out_of_cache:\t %lu\n"
	      "\t wrong_node:\t %lu\n"
	      "\t didnt_move:\t %lu\n"
	      "\t found:\t %lu\n"

	      "global:\n"
	      "\t non_uniq:\t %lu\n"
	      "\t non_uniq_max:\t %lu\n"
	      "\t stack_size_max:\t %lu\n"

	      "key:\n"
	      "\t eq0:\t %lu\n"
	      "\t eq1:\t %lu\n"
	      "\t eq2:\t %lu\n"
	      "\t eq3:\t %lu\n"

	      ,

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

	t = &get_current_super_private() -> tree;
	info( "spin locks:\n" );
	print_spin_lock( "\t tree     ", &t -> tree_lock ); info( "\n" );
	print_spin_lock( "\t dk       ", &t -> dk_lock ); info( "\n" );
	print_spin_lock( "\t cbk cache", &t -> cbk_cache.guard ); info( "\n" );

	for( i = 0 ; i < REAL_MAX_ZTREE_HEIGHT ; ++ i ) {
		if( s -> level[ i ].total_hits_at_level <= 0 )
			continue;
		info( "tree: at level: %i\n"
		      "\t carry_restart:\t %lu\n"
		      "\t carry_done:\t %lu\n"
		      "\t carry_left_in_carry:\t %lu\n"
		      "\t carry_left_in_cache:\t %lu\n"
		      "\t carry_left_missed:\t %lu\n"
		      "\t carry_left_not_avail:\t %lu\n"
		      "\t carry_left_refuse:\t %lu\n"
		      "\t carry_right_in_carry:\t %lu\n"
		      "\t carry_right_in_cache:\t %lu\n"
		      "\t carry_right_missed:\t %lu\n"
		      "\t carry_right_not_avail:\t %lu\n"
		      "\t insert_looking_left:\t %lu\n"
		      "\t insert_looking_right:\t %lu\n"
		      "\t insert_alloc_new:\t %lu\n"
		      "\t insert_alloc_many:\t %lu\n"
		      "\t insert:\t %lu\n"
		      "\t delete:\t %lu\n"
		      "\t cut:\t %lu\n"
		      "\t paste:\t %lu\n"
		      "\t extent:\t %lu\n"
		      "\t paste_restarted:\t %lu\n"
		      "\t update:\t %lu\n"
		      "\t modify:\t %lu\n"
		      "\t half_split_race:\t %lu\n"
		      "\t dk_vs_create_race:\t %lu\n"
		      "\t track_lh:\t %lu\n"
		      "\t sibling_search:\t %lu\n"
		      "\t cbk_key_moved:\t %lu\n"
		      "\t cbk_met_ghost:\t %lu\n",

		      i + LEAF_LEVEL,

		      s -> level[ i ].carry_restart,
		      s -> level[ i ].carry_done,
		      s -> level[ i ].carry_left_in_carry,
		      s -> level[ i ].carry_left_in_cache,
		      s -> level[ i ].carry_left_missed,
		      s -> level[ i ].carry_left_not_avail,
		      s -> level[ i ].carry_left_refuse,
		      s -> level[ i ].carry_right_in_carry,
		      s -> level[ i ].carry_right_in_cache,
		      s -> level[ i ].carry_right_missed,
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
		      s -> level[ i ].sibling_search,
		      s -> level[ i ].cbk_key_moved,
		      s -> level[ i ].cbk_met_ghost );
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
	__u32 flags;

	flags = reiser4_current_trace_flags;
	if( get_current_context() != NULL ) {
		flags |= get_current_context() -> trace_flags;
		if( get_current_super_private() != NULL )
			flags |= get_current_super_private() -> trace_flags;
	}

	return flags;
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
	assert( "nikita-1408", ergo( gfp_flag & __GFP_WAIT, 
				     lock_counters() -> spin_locked == 0 ) );

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
