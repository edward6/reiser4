/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* profiling facilities. */

#include "kattr.h"
#include "reiser4.h"
#include "context.h"
#include "super.h"
#include "prof.h"

#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>

#if REISER4_PROF

#ifdef CONFIG_FRAME_POINTER
static void 
update_prof_trace(reiser4_prof_cnt *cnt, int shift)
{
	int i;
	int minind;
	__u64 minhit;
	unsigned long hash;
	backtrace_path bt;

	fill_backtrace(&bt, shift);

	for (i = 0, hash = 0 ; i < REISER4_BACKTRACE_DEPTH ; ++ i) {
		hash += (unsigned long)bt.trace[i];
	}
	minhit = ~0ull;
	minind = 0;
	for (i = 0 ; i < REISER4_PROF_TRACE_NUM ; ++ i) {
		if (hash == cnt->bt[i].hash) {
			++ cnt->bt[i].hits;
			return;
		}
		if (cnt->bt[i].hits < minhit) {
			minhit = cnt->bt[i].hits;
			minind = i;
		}
	}
	cnt->bt[minind].path = bt;
	cnt->bt[minind].hash = hash;
	cnt->bt[minind].hits = 1;
}
#else
#define update_prof_trace(cnt, shift) noop
#endif

void update_prof_cnt(reiser4_prof_cnt *cnt, __u64 then, __u64 now, 
		     unsigned long swtch_mark, __u64 start_jif, int shift)
{
	__u64 delta;

	delta = now - then;
	cnt->nr ++;
	cnt->total += delta;
	cnt->max = max(cnt->max, delta);
	if (swtch_mark == nr_context_switches()) {
		cnt->noswtch_nr ++;
		cnt->noswtch_total += delta;
		cnt->noswtch_max = max(cnt->noswtch_max, delta);
	}
	update_prof_trace(cnt, shift);
}


static ssize_t 
show_prof_attr(struct kobject *kobj, struct attribute *attr, char *buf)
{
	char *p;
	reiser4_prof_entry *entry;
	reiser4_prof_cnt   *val;
#ifdef CONFIG_FRAME_POINTER
	int i;
#endif
	entry = container_of(attr, reiser4_prof_entry, attr);
	val = &entry->cnt;
	p = buf;
	KATTR_PRINT(p, buf, "%llu %llu %llu %llu %llu %llu\n",
		    val->nr, val->total, val->max,
		    val->noswtch_nr, val->noswtch_total, val->noswtch_max);
#ifdef CONFIG_FRAME_POINTER
	for (i = 0 ; i < REISER4_PROF_TRACE_NUM ; ++ i) {
		int j;

		if (val->bt[i].hash == 0)
			continue;

		KATTR_PRINT(p, buf, "\t%llu: ", val->bt[i].hits);
		for (j = 0 ; j < REISER4_BACKTRACE_DEPTH ; ++ j) {
			char         *module;
			const char   *name;
			char          namebuf[128];
			unsigned long address;
			unsigned long offset;
			unsigned long size;

			address = (unsigned long) val->bt[i].path.trace[j];
			name = kallsyms_lookup(address, &size, 
					       &offset, &module, namebuf);
			KATTR_PRINT(p, buf, "\n\t\t%#lx ", address);
			if (name != NULL)
				KATTR_PRINT(p, buf, "%s+%#lx/%#lx",
					    name, offset, size);
		}
		KATTR_PRINT(p, buf, "\n");
	}
#endif
	return (p - buf);
}

/* zero a prof entry corresponding to @attr */
static ssize_t 
store_prof_attr(struct kobject *kobj, struct attribute *attr, const char *buf, size_t size)
{
	reiser4_prof_entry *entry;

	entry = container_of(attr, reiser4_prof_entry, attr);
	memset(&entry->cnt, 0, sizeof(reiser4_prof_cnt));
	return sizeof(reiser4_prof_cnt);
}

static struct sysfs_ops prof_attr_ops = {
	.show = show_prof_attr,
	.store = store_prof_attr
};

static struct kobj_type ktype_reiser4_prof = {
	.sysfs_ops	= &prof_attr_ops,
	.default_attrs	= NULL
};

static decl_subsys(prof, &ktype_reiser4_prof, NULL);

static struct kobject spin_prof;

#define DEFINE_PROF_ENTRY_0(attr_name,field_name)	\
	.field_name = {					\
		.attr = {	       			\
			.name = (char *)attr_name,	\
			.mode = 0644 /* rw-r--r-- */	\
		}					\
	}


#define DEFINE_PROF_ENTRY(name)				\
 	DEFINE_PROF_ENTRY_0(#name,name)

reiser4_prof reiser4_prof_defs = {
	DEFINE_PROF_ENTRY(writepage),
	DEFINE_PROF_ENTRY(jload),
	DEFINE_PROF_ENTRY(jrelse),
	DEFINE_PROF_ENTRY(flush_alloc),
	DEFINE_PROF_ENTRY(forward_squalloc),
	DEFINE_PROF_ENTRY(atom_wait_event),
	DEFINE_PROF_ENTRY(zget),
	/* write profiling */
	DEFINE_PROF_ENTRY(extent_write),
	/* read profiling */
	DEFINE_PROF_ENTRY(file_read)
};

void calibrate_prof(void)
{
	__u64 start;
	__u64 end;

	rdtscll(start);
	schedule_timeout(HZ/100);
	rdtscll(end);
	warning("nikita-2923", "1 sec. == %llu rdtsc.", (end - start) * 100);
}


int init_prof_kobject(void)
{
	int result;

	result = subsystem_register(&prof_subsys);
	if (result == 0) {
		spin_prof.kset = &prof_subsys.kset;
		snprintf(spin_prof.name, KOBJ_NAME_LEN, "spin_prof");
		result = kobject_register(&spin_prof);
		if (result == 0) {
			/* populate */
			int i;
			reiser4_prof_entry *array;

			array = (reiser4_prof_entry *)&reiser4_prof_defs;
			for(i = 0 ; i < sizeof(reiser4_prof_defs)/sizeof(reiser4_prof_entry) && !result ; ++ i)
				result = sysfs_create_file(&spin_prof,
							   &array[i].attr);
		}
	}
	return result;
}

void done_prof_kobject(void)
{
	kobject_unregister(&spin_prof);
	subsystem_unregister(&prof_subsys);
}

/* REISER4_PROF */
#else

/* REISER4_PROF */
#endif
