/*
 * Copyright 2002 by Hans Reiser, licensing governed by reiser4/README
 */
/*
 * Transaction manager daemon.
 */

#include "reiser4.h"

static int scan_mgr( txn_mgr *mgr );

#if (1)
#define ktxnmgrd_trace( args... ) info( "ktxnmgrd: " ##args )
#else
#define ktxnmgrd_trace noop
#endif

/*
 * The background transaction manager daemon, started as a kernel thread
 * during reiser4 initialization.
 *
 */
int ktxnmgrd( void *arg )
{
	struct task_struct *me;
	ktxnmgrd_context   *ctx;

	/* standard kernel thread prologue */
	me = current;
	daemonize();
	reparent_to_init();
	strcpy( me -> comm, __FUNCTION__ );

	/* block all signals */
	spin_lock_irq( &me -> sigmask_lock );
	siginitsetinv( &me -> blocked, 0 );
	recalc_sigpending();
	spin_unlock_irq( &me -> sigmask_lock );

	/*
	 * do_fork() just copies task_struct into the new
	 * thread. ->journal_info shouldn't be copied of course. This shouldn't
	 * be a problem for the rest of the code though.
	 */
	me -> journal_info = NULL;

	ctx = arg;
	spin_lock( &ctx -> guard );
	ctx -> tsk = me;
	kcond_broadcast( &ctx -> startup );
	ktxnmgrd_trace( "started\n" );
	while( 1 ) {
		int      result;
		txn_mgr *mgr;

		/*
		 * software suspend support. Doesn't work currently
		 * (kcond_timedwait).
		 */
		if( me -> flags & PF_FREEZE )
			refrigerator( PF_IOTHREAD );

		/* While otherwise is good, kcond_timedwait is actually based on
		   semaphores, when task is waiting on semaphore, it is
		   converted to "UNINTERRUPTIBLE SLEEP" state, and each such
		   task adds one to LA, and this would confuse just about
		   everyone.
		   FIXME: I just fixed that with choosing not to ignore
		   signals, and handling -EINTR case absolutely the same as
		   in case of real timeout. */
		result = kcond_timedwait( &ctx -> wait, &ctx -> guard,
					  ctx -> timeout, 1 /* do not block signals */ );
		if( ( result != -ETIMEDOUT && result != -EINTR ) &&
		    ( result != 0 ) ) {
			/*
			 * some other error
			 */
			warning( "nikita-2443", "Error: %i", result );
			continue;
		}

		/*
		 * we are asked to exit
		 */
		if( ctx -> done )
			break;

		ktxnmgrd_trace( "woke up\n" );

		/*
		 * wait timed out or ktxnmgrd was woken up by explicit request
		 * to commit something. Scan list of atoms in txnmgr and look
		 * for too old atoms.
		 */
		do {
			ctx -> rescan = 0;
			for( mgr = txn_mgrs_list_front( &ctx -> queue ) ;
			     !txn_mgrs_list_end( &ctx -> queue, mgr ) ;
			     mgr = txn_mgrs_list_next( mgr ) ) {
				scan_mgr( mgr );
				ktxnmgrd_trace( "\tscan mgr\n" );
			}
			ktxnmgrd_trace( "scan finished\n" );
		} while( ctx -> rescan );
	}
	
	spin_unlock( &ctx -> guard );

	/*
	 * Leave with BKL locked. It will be unlocked within
	 * do_exit()/schedule(). This allows caller to avoid races between
	 * daemon completion and module unload. Standard way to do this is to
	 * use complete_and_exit() core function, but let us stick to kcond
	 * for now.
	 */
	lock_kernel();
	kcond_broadcast( &ctx -> finish );
	ktxnmgrd_trace( "exiting\n" );
	return 0;
}

void init_ktxnmgrd_context( ktxnmgrd_context *ctx )
{
	assert( "nikita-2442", ctx != NULL );

	xmemset( ctx, 0, sizeof *ctx );
	kcond_init( &ctx -> startup );
	kcond_init( &ctx -> finish );
	kcond_init( &ctx -> wait );
	spin_lock_init( &ctx -> guard );
	ctx -> timeout = REISER4_TXNMGR_TIMEOUT;
	txn_mgrs_list_init( &ctx -> queue );
}

static const unsigned int ktxnmrgd_flags = CLONE_VM | CLONE_FS | CLONE_FILES;

int ktxnmgrd_attach( ktxnmgrd_context *ctx, txn_mgr *mgr )
{
	int first_mgr;

	assert( "nikita-2448", mgr != NULL );

	spin_lock( &ctx -> guard );

	first_mgr = txn_mgrs_list_empty( &ctx -> queue );

	/*
	 * attach @mgr to daemon. Not taking spin-locks, because this is early
	 * during @mgr initialization.
	 */
	mgr -> daemon = ctx;
	txn_mgrs_list_push_back( &ctx -> queue, mgr );

	/*
	 * daemon thread is not yet initialized
	 */
	if( ctx -> tsk == NULL ) {
		/*
		 * attaching first mgr, start daemon
		 */
		if( first_mgr ) {
			ctx -> done = 0;

			/*
			 * kernel_thread never fails.
			 */
			kernel_thread( ktxnmgrd, ctx, ktxnmrgd_flags );
		}
		/*
		 * wait until initialization completes
		 */
		kcond_wait( &ctx -> startup, &ctx -> guard, 0 );
	}
	assert( "nikita-2452", ctx -> tsk != NULL );
	
	spin_unlock( &ctx -> guard );
	return 0;
}

void ktxnmgrd_detach( txn_mgr *mgr )
{
	ktxnmgrd_context *ctx;

	assert( "nikita-2450", mgr != NULL );

	/*
	 * this is supposed to happen when @mgr is quiesced and no locking is
	 * necessary.
	 */
	ctx = mgr -> daemon;
	if( ctx == NULL )
		return;

	spin_lock( &ctx -> guard );
	txn_mgrs_list_remove( mgr );
	mgr -> daemon = NULL;

	/*
	 * removing last mgr, stop daemon
	 */
	if( txn_mgrs_list_empty( &ctx -> queue ) ) {
		ctx -> tsk  = NULL;
		ctx -> done = 1;
		kcond_signal( &ctx -> wait );

		/*
		 * wait until daemon finishes
		 */
		lock_kernel();
		kcond_wait( &ctx -> finish, &ctx -> guard, 0 );
		unlock_kernel();
	}
	spin_unlock( &ctx -> guard );
}

/**
 * wake up ktxnmgrd thread
 */
void ktxnmgrd_kick( void )
{
	ktxnmgrd_context *ctx;

	assert( "nikita-2459", get_current_super_private() != NULL );

	ctx = get_current_super_private() -> tmgr.daemon;
	assert( "nikita-2460", ctx != NULL );
	spin_lock( &ctx -> guard );
	if( ctx -> tsk != NULL ) {
		trace_on( TRACE_TXN, "Waking ktxnmgrd %i", ctx -> tsk -> pid );
		kcond_signal( &ctx -> wait );
	}
	spin_unlock( &ctx -> guard );
}

/** scan one transaction manager for old atoms */
static int scan_mgr( txn_mgr *mgr )
{
	reiser4_tree *tree;
	assert( "nikita-2454", mgr != NULL );

	/*
	 * FIXME-NIKITA this only works for atoms embedded into super blocks.
	 */
	tree = &container_of( mgr, reiser4_super_info_data, tmgr ) -> tree;
	assert( "nikita-2455", tree != NULL );
	assert( "nikita-2456", tree -> super != NULL );

	{
		REISER4_ENTRY( tree -> super );
		REISER4_EXIT ( txn_commit_some( mgr ) );
	}
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
