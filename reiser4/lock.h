/* -*- c -*- */ 

/*
 *
 * Copyright 2001 Hans Reiser <reiser@namesys.com>
 *
 */

/* reiser4 lock protocol is described in lock.c file */

#ifndef _FS_REISER4_LOCK_H
#define _FS_REISER4_LOCK_H

/* data types forward declaration */

typedef struct __reiser4_zlock reiser4_zlock;
typedef struct __reiser4_lock_stack reiser4_lock_stack;
typedef struct __reiser4_lock_handle reiser4_lock_handle;

/** type of lock to acquire on znode before returning it to caller */
typedef enum { znode_read_lock, znode_write_lock } znode_lock_type;

/*        Lock compatibility table
 *               read write
 *             + ----------
 * no lock     |  yes  yes
 * read        |  yes  no
 * write       |  no   no
 *
 */

/* since we have R/W znode locks we need addititional `link' objects to
   implement n<->m relationship between lock stacks and lock objects. We name
   them as lock handles. */

struct __reiser4_lock_handle {
	reiser4_lock_stack *stack;
	reiser4_zlock *lock;
	struct list_head lock_handles_chain;
	struct list_head stack_links_chain;
};

/* lock object, usually per-znode, also for the sb tree root pointer */
struct __reiser4_zlock {
	spinlock_t guard;	/* short-term lock that protects lockobject fields in SMP
				   environment. */
	int nr_readers;		/* numbers of readers; -1 for single writer; -2 for invalidated lock */
	int nr_hipri_lockers;	/* number of processes (lock_stacks) that have this object locked with high priority */
	struct list_head stack_links; /* linked list of lock_handle objects that contains pointers for all lock_stacks
					 which have this lock object locked */
	int nr_hipri_requests;	/* number of hipri requests */
	struct list_head requestors; /* linked list of lock_stacks that wait this lock.  we need list of all processes
				     (represented through lock_stacks) that want to acquire lock on this object to
				     implement lock passing procedure */
};

/* lock objects are accumulated on a lock_stack object;  */
struct __reiser4_lock_stack {
	int nr_locked;
	struct list_head lock_handles; /* same link objects are used for finding all locks for given
					lock_stack */
	struct list_head requestors_chain; /* when lock_stack waits for the lock, it puts itself on
					      double-linked requestors list of that lock;
					      requestors_chain is a service field to support that
					      list on lock_stack side. */
	/* current lock request info */
	struct {
		reiser4_lock_handle * link; /* pointer to uninitialized link object */
		reiser4_zlock * lock;     /* --"--   to object we want to lock */
		int mode;
		int hipri;
	} request;
	atomic_t requested;	/* a lock (or locks) were requested */
	/* It is a lock_stack's synchronization object for when process sleeps when requested lock
	   not on this lock_stack but which it wishes to add to this lock_stack is not immediately
	   available. It is used instead of wait_queue_t object due to locking problems (lost
	   wake up). "lost wakeup" occurs when process is waken up before he actually becomes
	   'sleepy' (through sleep_on()). Using of semaphore object is simplest way to avoid that
	   problem.

	   A semaphore is used in the following way: only the process that is the owner of the
	   lock_stack initializes it (to zero) and calls down(sema) on it. Usually this causes the
	   process to sleep on the semaphore. Other processes may wake him up by calling
	   up(sema). The advantage to a semaphore is that up() and down() calls are not required to
	   preserve order. Unlike wait_queue it works when process is woken up before getting to
	   sleep. 
	*/
	struct semaphore sema;
};

/* check if high priority lockers may (and should) request this lock object */
static inline int lock_is_requested (reiser4_zlock * lock)
{
	return lock->nr_hipri_requests > 0 && lock->nr_hipri_lockers == 0;
}


/* top-level functions for znode and other objects locking */

int reiser4_lock_lock (reiser4_lock_handle * result,
		       reiser4_zlock * lock, int mode, int hipri);

void reiser4_unlock_lock (reiser4_lock_handle * lh);

void reiser4_invalidate_lock (reiser4_lock_handle * handle);

void reiser4_check_requested ();

void reiser4_fix_lock_stack (int new_nr_locked);

int reiser4_get_nr_locked ();

void reiser4_copy_lh ( reiser4_lock_handle * new, reiser4_lock_handle * old);

reiser4_lock_handle * reiser4_get_first_lock();

reiser4_lock_handle * reiser4_get_last_lock();

/**
 * lock_stack structure initialization.
 * @stack: pointer to allocated structure.
 */
static inline void reiser4_init_lock_stack (reiser4_lock_stack * stack)
{
	memset(stack, 0, sizeof(reiser4_lock_stack));
	INIT_LIST_HEAD(&stack->lock_handles);
}
 
/**
 * lock object initialization.
 * @lock: pointer on allocated uninitialized lock object structure.
 */
static inline void reiser4_init_lock (reiser4_zlock * lock)
{
	memset(lock, 0, sizeof(reiser4_zlock));
	INIT_LIST_HEAD(&lock->requestors);
	INIT_LIST_HEAD(&lock->stack_links);
}

#endif /* _FS_REISER4_LOCK_H */

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
