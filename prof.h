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

#define REISER4_PROF_TRACE_NUM (8)

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
	struct attribute attr;
	reiser4_prof_cnt cnt;
} reiser4_prof_entry;

typedef struct {
	reiser4_prof_entry jload;
	reiser4_prof_entry jrelse;
	reiser4_prof_entry carry;
	reiser4_prof_entry flush_alloc;
	reiser4_prof_entry forward_squalloc;
	reiser4_prof_entry atom_wait_event;
	reiser4_prof_entry set_child_delimiting_keys;
	reiser4_prof_entry zget;
	/* write profiling */
	reiser4_prof_entry extent_write;
	reiser4_prof_entry copy;
	reiser4_prof_entry extent_bdp;
	reiser4_prof_entry set_seal;
	reiser4_prof_entry update_sd;
	reiser4_prof_entry bdp;
	reiser4_prof_entry validate;
	reiser4_prof_entry seal_validate;
	reiser4_prof_entry update_sd_load;
	reiser4_prof_entry update_sd_save;
	reiser4_prof_entry update_sd_seal;
	/* read profiling */
	reiser4_prof_entry file_read;
	reiser4_prof_entry prep;
	reiser4_prof_entry read_grab;
	reiser4_prof_entry build_flow;
	reiser4_prof_entry load_hint;
	reiser4_prof_entry find;
	reiser4_prof_entry zload;
	reiser4_prof_entry item_read;
	reiser4_prof_entry set_hint;
	reiser4_prof_entry save_hint;
	reiser4_prof_entry copy_to_user;
} reiser4_prof;

extern reiser4_prof reiser4_prof_defs;

extern unsigned long nr_context_switches(void);
void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now, 
		     unsigned long swtch_mark, __u64 start_jif);
void calibrate_prof(void);

#define PROF_BEGIN(aname)							\
	unsigned long __swtch_mark__ ## aname = nr_context_switches();		\
        __u64 __prof_jiffies ## aname = jiffies;				\
	__u64 __prof_cnt__ ## aname = ({ __u64 __tmp_prof ;			\
			      		rdtscll(__tmp_prof) ; __tmp_prof; })

#define PROF_END(aname, acnt)							\
({										\
	__u64 __prof_end;							\
										\
	rdtscll(__prof_end);							\
	update_prof_cnt(&reiser4_prof_defs.acnt.cnt, __prof_cnt__ ## aname, __prof_end,		\
			__swtch_mark__ ## aname, __prof_jiffies ## aname );	\
})

#else

typedef struct reiser4_prof_cnt {} reiser4_prof_cnt;
typedef struct reiser4_prof {} reiser4_prof;

#define PROF_BEGIN(aname) noop
#define PROF_END(aname, acnt) noop
#define calibrate_prof() noop

#endif

/* __REISER4_PROF_H__ */
#endif
