/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */

#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"
#include "spin_macros.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#ifdef CONFIG_PROFILING

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
	int top;
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

	preempt_disable();
	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == PROFREGION_MAX_DEPTH);
	profregfill(&stack->stack[stack->top++], pregion, objloc, codeloc);
}

static inline void profregion_ex(int cpu, struct profregion *pregion)
{
	struct profregionstack *stack;

	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == 0);
	if(likely(stack->stack[stack->top - 1].preg == pregion)) {
		int ntop;

		ntop = stack->top;
		do {
			-- ntop;
		} while (ntop > 0 &&
			 stack->stack[ntop - 1].preg == NULL);
		stack->top = ntop;
	} else
		stack->stack[profregion_find(stack, pregion)].preg = NULL;
	preempt_enable();
	put_cpu();
}

static inline void profregion_replace(int cpu, struct profregion *pregion,
				      void *objloc, void *codeloc)
{
	struct profregionstack *stack;

	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == 0);
	profregfill(&stack->stack[stack->top - 1], pregion, objloc, codeloc);
}

extern int  profregion_register(struct profregion *pregion);
extern void profregion_unregister(struct profregion *pregion);

/* CONFIG_PROFILING */
#else

struct profregionstack {};
#define profregion_register(pregion) (0)
#define profregion_unregister(pregion) noop

typedef struct locksite {} locksite;
#define LOCKSITE_INIT(name) extern locksite name

/* CONFIG_PROFILING */
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
