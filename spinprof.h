/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */

#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"
#include "spin_macros.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#if REISER4_LOCKPROF

#define PROFREGION_MAX_DEPTH (12)

typedef struct locksite {
	int   hits;
	const char *func;
	const char *file;
	int   line;
} locksite;

#define LOCKSITE_INIT(name)			\
	static locksite name = {		\
		.hits = 0,			\
		.func = __FUNCTION__,		\
		.file = __FILE__,		\
		.line = __LINE__		\
	}

struct profregion {
	int            hits;
	int            busy;
	struct kobject kobj;
	void          *obj;
	int            objhit;
	locksite      *code;
	int            codehit;
	void (*champion)(struct profregion * preg);
};


struct pregactivation {
	struct profregion *preg;
	int               *objloc;
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
#define LOCKSITE_INIT(name) static locksite name

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
