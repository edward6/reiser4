/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */

#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"
#include "spin_macros.h"
#include "statcnt.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#if REISER4_LOCKPROF

#define PROFREGION_MAX_DEPTH (12)

typedef struct percpu_counter scnt_t;

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

struct profregion {
	statcnt_t      hits;
	statcnt_t      busy;
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
