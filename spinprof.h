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
	char *func;
	char *file;
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
	struct kobject kobj;
	void          *obj;
	int            objhit;
	locksite      *code;
	int            codehit;
};


struct pregactivation {
	struct profregion *preg;
	int               *objloc;
	locksite          *codeloc;
};

struct profregionstack {
	atomic_t top;
	struct pregactivation stack[PROFREGION_MAX_DEPTH];
};

DECLARE_PER_CPU(struct profregionstack, inregion);

extern int profregion_find(struct profregionstack *stack, 
			   struct profregion *pregion);

static inline void profregfill(struct pregactivation *act,
			       struct profregion *pregion,
			       void *objloc, void *codeloc)
{
	act->preg    = pregion;
	act->objloc  = objloc;
	act->codeloc = codeloc;
}

static inline void profregion_in(int cpu, struct profregion *pregion,
				 void *objloc, locksite *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	preempt_disable();
	stack = &per_cpu(inregion, cpu);
	ntop = atomic_read(&stack->top);
	BUG_ON(ntop == PROFREGION_MAX_DEPTH);
	profregfill(&stack->stack[ntop], pregion, objloc, codeloc);
	atomic_inc(&stack->top);
}

static inline void profregion_ex(int cpu, struct profregion *pregion)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = atomic_read(&stack->top);
	BUG_ON(ntop == 0);
	if(likely(stack->stack[ntop - 1].preg == pregion)) {
		do {
			-- ntop;
		} while (ntop > 0 &&
			 stack->stack[ntop - 1].preg == NULL);
		atomic_set(&stack->top, ntop);
	} else
		stack->stack[profregion_find(stack, pregion)].preg = NULL;
	preempt_enable();
	put_cpu();
}

static inline void profregion_replace(int cpu, struct profregion *pregion,
				      void *objloc, void *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = atomic_read(&stack->top);
	BUG_ON(ntop == 0);
	profregfill(&stack->stack[ntop - 1], pregion, objloc, codeloc);
}

extern int  profregion_register(struct profregion *pregion);
extern void profregion_unregister(struct profregion *pregion);

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
