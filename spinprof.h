/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */

#ifndef __SPINPROF_H__
#define __SPINPROF_H__

#include "debug.h"

#include <linux/config.h>
#include <linux/profile.h>
#include <linux/kobject.h>

#ifdef CONFIG_PROFILING

#define PROFREGION_MAX_DEPTH (12)

struct profregion {
	int                 hits;
	struct  kobject     kobj;
	struct  profregion *prev[NR_CPUS];
};

struct profregionstack {
	int top;
	struct profregion *stack[PROFREGION_MAX_DEPTH];
};

DECLARE_PER_CPU(struct profregionstack, inregion);

static inline void profregion_in(struct profregion *pregion)
{
	struct profregionstack *stack;
	int cpu;

	cpu = get_cpu();
	preempt_disable();
	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == PROFREGION_MAX_DEPTH);
	stack->stack[stack->top++] = pregion;
	put_cpu();
}

static inline void profregion_ex(void)
{
	struct profregionstack *stack;
	int cpu;

	cpu = get_cpu();
	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == 0);
	-- stack->top;
	preempt_enable();
	put_cpu();
}

static inline void profregion_replace(struct profregion *pregion)
{
	struct profregionstack *stack;
	int cpu;

	cpu = get_cpu();
	stack = &per_cpu(inregion, cpu);
	BUG_ON(stack->top == 0);
	stack->stack[stack->top - 1] = pregion;
	put_cpu();
}

extern int  profregion_register(struct profregion *pregion);
extern void profregion_unregister(struct profregion *pregion);

/* CONFIG_PROFILING */
#else

struct profregionstack {};
#define profregion_register(pregion) (0)
#define profregion_unregister(pregion) noop

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
