/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* spin lock profiling */

/*
 * Spin-lock profiling code.
 *
 * Basic notion in our profiling code is "profiling region" (struct
 * profregion). Profiling region is entered and left by calling
 * profregion_in() and profregion_ex() function correspondingly. It is invalid
 * to be preempted (voluntary or not) while inside profiling region. Profiling
 * regions can be entered recursively, and it is not necessary nest then
 * properly, that is
 *
 *     profregion_in(&A);
 *     profregion_in(&B);
 *     profregion_ex(&A);
 *     profregion_ex(&B))
 *
 * is valid sequence of operations. Each CPU maintains an array of currently
 * active profiling regions. This array is consulted by clock interrupt
 * handler, and counters in the profiling regions found active by handler are
 * incremented. This allows one to estimate for how long region has been
 * active on average. Spin-locking code in spin_macros.h uses this to measure
 * spin-lock contention. Specifically two profiling regions are defined for
 * each spin-lock type: one is activate while thread is trying to acquire
 * lock, and another when it holds the lock. Profiling regions export their
 * statistics in the sysfs.
 *
 */
#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"
#include "spin_macros.h"
#include "statcnt.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#if REISER4_LOCKPROF

/* maximal number of profiling regions that can be active at the same time */
#define PROFREGION_MAX_DEPTH (12)

typedef struct percpu_counter scnt_t;

/* spin-locking code uses this to identify place in the code, where particular
 * call to locking function is made. */
typedef struct locksite {
	statcnt_t   hits;
	const char *func;
	int         line;
} locksite;

#define LOCKSITE_INIT(name)			\
	static locksite name = {		\
		.hits = STATCNT_INIT,		\
		.func = __FUNCTION__,		\
		.line = __LINE__		\
	}

/* profiling region */
struct profregion {
	/* how many times clock interrupt handler found this profiling region
	 * to be at the top of array of active regions. */
	statcnt_t      hits;
	/* how many times clock interrupt handler found this profiling region
	 * in active array */
	statcnt_t      busy;
	/* sysfs handle */
	struct kobject kobj;
	void          *obj;
	int            objhit;
	locksite      *code;
	int            codehit;
	void (*champion)(struct profregion * preg);
};


struct pregactivation {
	/* profiling region */
	struct profregion *preg;
	/* pointer to hits counter, embedded into object */
	int               *objloc;
	/* current lock site */
	locksite          *codeloc;
};

struct profregionstack {
	int top;
	struct pregactivation stack[PROFREGION_MAX_DEPTH];
};

DECLARE_PER_CPU(struct profregionstack, inregion);

extern int  profregion_register(struct profregion *pregion);
extern void profregion_unregister(struct profregion *pregion);

extern void profregion_in(int cpu, struct profregion *pregion,
			  void *objloc, locksite *codeloc);
extern void profregion_ex(int cpu, struct profregion *pregion);
extern void profregion_replace(int cpu, struct profregion *pregion,
			       void *objloc, void *codeloc);

/* REISER4_LOCKPROF */
#else

struct profregionstack {};
#define profregion_register(pregion) (0)
#define profregion_unregister(pregion) noop

typedef struct locksite {} locksite;
#define LOCKSITE_INIT(name) extern locksite name

/* REISER4_LOCKPROF */
#endif

/* __SPINPROF_H__ */
#endif
/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
