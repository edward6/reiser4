/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Efficient counter for statistics collection. */

#ifndef __STATCNT_H__
#define __STATCNT_H__

#include <linux/types.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/threads.h>

#ifdef CONFIG_SMP

struct __statcnt_slot {
	long count;
} ____cacheline_aligned;

typedef struct statcnt {
	struct __statcnt_slot counters[NR_CPUS];
} statcnt_t;

#define STATCNT_INIT						\
{								\
	.counters = { [ 0 ... NR_CPUS - 1 ] = { .count = 0 } }	\
}

static inline void statcnt_add(statcnt_t *cnt, int val)
{
	int cpu;

	cpu = get_cpu();
	cnt->counters[cpu].count += val;
	put_cpu();
}

static inline long statcnt_get(statcnt_t *cnt)
{
	long result;
	int  i;

	for (i = 0, result = 0; i < NR_CPUS; ++i)
		result += cnt->counters[i].count;
	return result;
}

/* CONFIG_SMP */
#else

typedef struct statcnt {
	long count;
} statcnt_t;

#define STATCNT_INIT { .count = 0 }

static inline void statcnt_add(statcnt_t *cnt, int val)
{
	cnt->count += val;
}

static inline long statcnt_get(statcnt_t *cnt)
{
	return cnt->count;
}

/* CONFIG_SMP */
#endif

static inline void statcnt_init(statcnt_t *cnt)
{
	xmemset(cnt, 0, sizeof *cnt);
}

#define statcnt_reset(cnt) statcnt_init(cnt)

#define statcnt_inc(cnt) statcnt_add((cnt), +1)
#define statcnt_dec(cnt) statcnt_add((cnt), -1)

/* __STATCNT_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
