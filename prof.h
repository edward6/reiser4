/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __REISER4_PROF_H__ )
#define __REISER4_PROF_H__

#include "kattr.h"

/* profiling. This is i386, rdtsc-based profiling. */

#if (defined(__i386__) || defined(CONFIG_USERMODE)) && defined(CONFIG_REISER4_PROF)
#define REISER4_PROF (1)
#else
#define REISER4_PROF (0)
#endif

#if REISER4_PROF

#include <asm-i386/msr.h>

#define REISER4_PROF_TRACE_NUM (30)

typedef struct {
	unsigned long hash;
	backtrace_path path;
	__u64 hits;
} reiser4_trace;

typedef struct {
	__u64 nr;
	__u64 total;
	__u64 max;
	__u64 noswtch_nr;
	__u64 noswtch_total;
	__u64 noswtch_max;
	reiser4_trace bt[REISER4_PROF_TRACE_NUM];
} reiser4_prof_cnt;

typedef struct {
	struct kobject kobj;
	reiser4_prof_cnt cnt;
} reiser4_prof_entry;

typedef struct {
	reiser4_prof_entry cbk;
#if 0
	reiser4_prof_entry init_context;
	reiser4_prof_entry jlook;
	reiser4_prof_entry writepage;
	reiser4_prof_entry jload;
	reiser4_prof_entry jrelse;
	reiser4_prof_entry flush_alloc;
	reiser4_prof_entry forward_squalloc;
	reiser4_prof_entry atom_wait_event;
	reiser4_prof_entry zget;
	/* write profiling */
	reiser4_prof_entry extent_write;
	/* read profiling */
	reiser4_prof_entry file_read;
#endif
} reiser4_prof;

extern reiser4_prof reiser4_prof_defs;

extern unsigned long nr_context_switches(void);
void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now, 
		     unsigned long swtch_mark, __u64 start_jif, 
		     int delta, int shift);
void calibrate_prof(void);

#define PROF_BEGIN(aname)							\
	unsigned long __swtch_mark__ ## aname = nr_context_switches();		\
        __u64 __prof_jiffies ## aname = jiffies;				\
	__u64 __prof_cnt__ ## aname = ({ __u64 __tmp_prof ;			\
			      		rdtscll(__tmp_prof) ; __tmp_prof; })

#define PROF_END(aname) __PROF_END(aname, REISER4_BACKTRACE_DEPTH, 0)

#define __PROF_END(aname, depth, shift)			\
({							\
	__u64 __prof_end;				\
							\
	rdtscll(__prof_end);				\
	update_prof_cnt(&reiser4_prof_defs.aname.cnt, 	\
			__prof_cnt__ ## aname,		\
			__prof_end,			\
			__swtch_mark__ ## aname, 	\
			__prof_jiffies ## aname, 	\
			depth, shift );			\
})

extern int init_prof_kobject(void);
extern void done_prof_kobject(void);

/* REISER4_PROF */
#else

typedef struct reiser4_prof_cnt {} reiser4_prof_cnt;
typedef struct reiser4_prof {} reiser4_prof;

#define PROF_BEGIN(aname) noop
#define PROF_END(aname) noop
#define __PROF_END(aname, depth, shift) noop
#define calibrate_prof() noop

#define init_prof_kobject() (0)
#define done_prof_kobject() noop

#endif

/* __REISER4_PROF_H__ */
#endif
