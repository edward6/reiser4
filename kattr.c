/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to sysfs' attributes */

#include "debug.h"
#include "super.h"
#include "kattr.h"
#include "prof.h"

#include <linux/kobject.h>     /* struct kobject */
#include <linux/fs.h>          /* struct super_block */

#if REISER4_USE_SYSFS

static inline reiser4_kattr *
to_kattr(struct attribute *attr)
{
	return container_of(attr, reiser4_kattr, attr);
}

static inline struct super_block *
to_super(struct kobject *kobj)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = container_of(kobj, reiser4_super_info_data, kobj);
	return sbinfo->tree.super;
}

static ssize_t
kattr_show(struct kobject *kobj, struct attribute *attr,  char *buf)
{
	struct super_block *super;
	reiser4_kattr *kattr;

	super = to_super(kobj);
	kattr = to_kattr(attr);

	if (kattr->show != NULL)
		return kattr->show(super, kattr, 0, buf);
	else
		return 0;
}

typedef struct {
	ptrdiff_t   offset;
	const char *format;
} super_field_cookie;

#define DEFINE_SUPER_RO(aname, afield, aformat, asize)		\
static super_field_cookie __cookie_ ## aname = {		\
	.offset = offsetof(reiser4_super_info_data, afield),	\
	.format = aformat "\n"					\
};								\
								\
static reiser4_kattr kattr_super_ro_ ## aname = {		\
	.attr = {						\
		.name = (char *) #afield,			\
		.mode = 0444   /* r--r--r-- */			\
	},							\
	.cookie = &__cookie_ ## aname,				\
	.show = show_ro_ ## asize				\
}

#define getat(ptr, offset, type) *(type *)(((char *)(ptr)) + (offset))

static ssize_t 
show_ro_32(struct super_block * s, reiser4_kattr * kattr, void * o, char * buf)
{
	char *p;
	super_field_cookie *cookie;
	__u32 val;

	(void)o;

	cookie = kattr->cookie;
	val = getat(get_super_private(s), cookie->offset, __u32);
	p = buf;
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

static ssize_t show_ro_64(struct super_block * s, 
			  reiser4_kattr * kattr, void * opaque, char * buf)
{
	char *p;
	super_field_cookie *cookie;
	__u64 val;

	(void)opaque;

	cookie = kattr->cookie;
	val = getat(get_super_private(s), cookie->offset, __u64);
	p = buf;
	KATTR_PRINT(p, buf, cookie->format, (unsigned long long)val);
	return (p - buf);
}

#define SHOW_OPTION(p, buf, option)			\
	if (option)					\
		KATTR_PRINT((p), (buf), #option "\n")

static ssize_t 
show_options(struct super_block * s, reiser4_kattr * kattr, void * o, char * buf)
{
	char *p;

	(void)o;
	p = buf;

	SHOW_OPTION(p, buf, REISER4_DEBUG);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MODIFY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_MEMCPY);
	SHOW_OPTION(p, buf, REISER4_DEBUG_NODE);
	SHOW_OPTION(p, buf, REISER4_ZERO_NEW_NODE);
	SHOW_OPTION(p, buf, REISER4_TRACE);
	SHOW_OPTION(p, buf, REISER4_TRACE_TREE);
	SHOW_OPTION(p, buf, REISER4_STATS);
	SHOW_OPTION(p, buf, REISER4_DEBUG_OUTPUT);
	SHOW_OPTION(p, buf, REISER4_USE_EFLUSH);
	SHOW_OPTION(p, buf, REISER4_LOCKPROF);
	SHOW_OPTION(p, buf, REISER4_LARGE_KEY);
	SHOW_OPTION(p, buf, REISER4_PROF);
	return (p - buf);
}

static reiser4_kattr compile_options = {
	.attr = {
		.name = (char *) "options",
		.mode = 0444   /* r--r--r-- */
	},
	.cookie = NULL,
	.show = show_options
};

DEFINE_SUPER_RO(01, mkfs_id, "%llx", 32);
DEFINE_SUPER_RO(02, block_count, "%llu", 64);
DEFINE_SUPER_RO(03, blocks_used, "%llu", 64);
DEFINE_SUPER_RO(04, blocks_free_committed, "%llu", 64);
DEFINE_SUPER_RO(05, blocks_grabbed, "%llu", 64);
DEFINE_SUPER_RO(06, blocks_fake_allocated_unformatted, "%llu", 64);
DEFINE_SUPER_RO(07, blocks_fake_allocated, "%llu", 64);
DEFINE_SUPER_RO(08, blocks_flush_reserved, "%llu", 64);
DEFINE_SUPER_RO(09, fsuid, "%llx", 32);
DEFINE_SUPER_RO(10, eflushed, "%llu", 32);
DEFINE_SUPER_RO(11, blocknr_hint_default, "%lli", 64);
DEFINE_SUPER_RO(12, nr_files_committed, "%llu", 64);
DEFINE_SUPER_RO(13, tmgr.atom_count, "%llu", 32);
DEFINE_SUPER_RO(14, tmgr.id_count, "%llu", 32);
DEFINE_SUPER_RO(15, tmgr.atom_max_size, "%llu", 32);
DEFINE_SUPER_RO(16, tmgr.atom_max_age, "%llu", 32);

/* tree fields */
DEFINE_SUPER_RO(17, tree.root_block, "%llu", 64);
DEFINE_SUPER_RO(18, tree.height, "%llu", 32);
DEFINE_SUPER_RO(19, tree.znode_epoch, "%llu", 64);
DEFINE_SUPER_RO(20, tree.carry.new_node_flags, "%llx", 32);
DEFINE_SUPER_RO(21, tree.carry.new_extent_flags, "%llx", 32);
DEFINE_SUPER_RO(22, tree.carry.paste_flags, "%llx", 32);
DEFINE_SUPER_RO(23, tree.carry.insert_flags, "%llx", 32);

/* not very good. Should be done by the plugin in stead */
DEFINE_SUPER_RO(24, next_to_use, "%llu", 64);
DEFINE_SUPER_RO(25, oids_in_use, "%llu", 64);

DEFINE_SUPER_RO(26, entd.flushers, "%llu", 32);
DEFINE_SUPER_RO(27, entd.timeout, "%llu", 32);

static struct attribute * def_attrs[] = {
	&kattr_super_ro_01.attr,
	&kattr_super_ro_02.attr,
	&kattr_super_ro_03.attr,
	&kattr_super_ro_04.attr,
	&kattr_super_ro_05.attr,
	&kattr_super_ro_06.attr,
	&kattr_super_ro_07.attr,
	&kattr_super_ro_08.attr,
	&kattr_super_ro_09.attr,
	&kattr_super_ro_10.attr,
	&kattr_super_ro_11.attr,
	&kattr_super_ro_12.attr,
	&kattr_super_ro_13.attr,
	&kattr_super_ro_14.attr,
	&kattr_super_ro_15.attr,
	&kattr_super_ro_16.attr,
	&kattr_super_ro_17.attr,
	&kattr_super_ro_18.attr,
	&kattr_super_ro_19.attr,
	&kattr_super_ro_20.attr,
	&kattr_super_ro_21.attr,
	&kattr_super_ro_22.attr,
	&kattr_super_ro_23.attr,
	&kattr_super_ro_24.attr,
	&kattr_super_ro_25.attr,
	&kattr_super_ro_26.attr,
	&kattr_super_ro_27.attr,
	&compile_options.attr,
	NULL
};

static struct sysfs_ops attr_ops = {
	.show  = kattr_show,
	.store = NULL
};

struct kobj_type ktype_reiser4 = {
	.sysfs_ops	= &attr_ops,
	.default_attrs	= def_attrs,
	.release	= NULL
};

#if REISER4_STATS

static ssize_t
kattr_stats_show(struct kobject *kobj, struct attribute *attr,  char *buf)
{
	reiser4_super_info_data *sbinfo;
	reiser4_kattr *kattr;

	sbinfo = container_of(kobj, reiser4_super_info_data, stats_kobj);
	kattr = to_kattr(attr);

	if (kattr->show != NULL)
		return kattr->show(sbinfo->tree.super, kattr, 0, buf);
	else
		return 0;
}

static ssize_t
kattr_stats_store(struct kobject *kobj, struct attribute *attr, 
		  const char *buf, size_t size)
{
	reiser4_super_info_data *sbinfo;
	reiser4_kattr *kattr;

	sbinfo = container_of(kobj, reiser4_super_info_data, stats_kobj);
	kattr = to_kattr(attr);

	if (kattr->store != NULL)
		return kattr->store(sbinfo->tree.super, kattr, 0, buf, size);
	else
		return 0;
}


static struct sysfs_ops stats_attr_ops = {
	.show  = kattr_stats_show,
	.store = kattr_stats_store
};

static struct kobj_type ktype_noattr = {
	.sysfs_ops	= &stats_attr_ops,
	.default_attrs	= NULL,
	.release        = NULL
};

static ssize_t
kattr_level_show(struct kobject *kobj, struct attribute *attr,  char *buf)
{
	reiser4_super_info_data *sbinfo;
	reiser4_level_stats_kobj *level_kobj;
	int level;
	reiser4_kattr *kattr;

	level_kobj = container_of(kobj, reiser4_level_stats_kobj, kobj);
	level = level_kobj->level;
	level_kobj -= level;
	sbinfo = container_of(level_kobj, reiser4_super_info_data, level[0]);
	kattr = to_kattr(attr);

	if (kattr->show != NULL)
		return kattr->show(sbinfo->tree.super, kattr, &level, buf);
	else
		return 0;
}

static ssize_t
kattr_level_store(struct kobject *kobj, struct attribute *attr, 
		  const char *buf, size_t size)
{
	reiser4_super_info_data *sbinfo;
	reiser4_level_stats_kobj *level_kobj;
	int level;
	reiser4_kattr *kattr;

	level_kobj = container_of(kobj, reiser4_level_stats_kobj, kobj);
	level = level_kobj->level;
	level_kobj -= level;
	sbinfo = container_of(level_kobj, reiser4_super_info_data, level[0]);
	kattr = to_kattr(attr);

	if (kattr->store != NULL)
		return kattr->store(sbinfo->tree.super, kattr, &level, buf, size);
	else
		return 0;
}

static struct sysfs_ops attr_level_ops = {
	.show  = kattr_level_show,
	.store = kattr_level_store
};

static struct kobj_type ktype_level_reiser4 = {
	.sysfs_ops	= &attr_level_ops,
	.default_attrs	= NULL,
	.release        = NULL
};

static int register_level_attrs(reiser4_super_info_data *sbinfo, int i)
{
	struct kobject *parent;
	struct kobject *level;
	int result;

	parent = &sbinfo->stats_kobj;
	sbinfo->level[i].level = i;
	level = &sbinfo->level[i].kobj;
	level->parent = kobject_get(parent);
	if (level->parent != NULL) {
		snprintf(level->name, 
			 KOBJ_NAME_LEN, "level-%2.2i", i);
		level->ktype = &ktype_level_reiser4;
		result = kobject_register(level);
		if (result == 0)
			result = reiser4_populate_kattr_level_dir(level);
	} else
		result = -EBUSY;
	return result;
}
#endif

static decl_subsys(fs, NULL, NULL);
decl_subsys(reiser4, &ktype_reiser4, NULL);

int reiser4_sysfs_init_once(void)
{
	int result;

	result = subsystem_register(&fs_subsys);
	if (result == 0) {
		kset_set_kset_s(&reiser4_subsys, fs_subsys);
		result = subsystem_register(&reiser4_subsys);
		if (result == 0)
			result = init_prof_kobject();
	}
	return result;
}

void reiser4_sysfs_done_once(void)
{
	subsystem_unregister(&reiser4_subsys);
	subsystem_unregister(&fs_subsys);
	done_prof_kobject();
}

int reiser4_sysfs_init(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	struct kobject *kobj;
	int result;
	ON_STATS(struct kobject *stats_kobj);

	sbinfo = get_super_private(super);

	kobj = &sbinfo->kobj;

	snprintf(kobj->name, KOBJ_NAME_LEN, "%s", super->s_id);
	kobj_set_kset_s(sbinfo, reiser4_subsys);
	result = kobject_register(kobj);
	if (result != 0)
		return result;
#if REISER4_STATS
	/* add attributes representing statistical counters */
	stats_kobj = &sbinfo->stats_kobj;
	stats_kobj->parent = kobject_get(kobj);
	snprintf(stats_kobj->name, KOBJ_NAME_LEN, "stats");
	stats_kobj->ktype = &ktype_noattr;
	result = kobject_register(stats_kobj);
	if (result != 0)
		return result;
	result = reiser4_populate_kattr_dir(stats_kobj);
	if (result == 0) {
		int i;

		for (i = 0; i < sizeof_array(sbinfo->level); ++i) {
			result = register_level_attrs(sbinfo, i);
			if (result != 0)
				break;
		}
	}
#else
	result = reiser4_populate_kattr_dir(kobj);
#endif

	return result;
}

void reiser4_sysfs_done(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	ON_STATS(int i);

	sbinfo = get_super_private(super);
#if REISER4_STATS
	for (i = 0; i < sizeof_array(sbinfo->level); ++i)
		kobject_unregister(&sbinfo->level[i].kobj);
	kobject_unregister(&sbinfo->stats_kobj);
#endif
	kobject_unregister(&sbinfo->kobj);
}

/* REISER4_USE_SYSFS */
#else

int reiser4_sysfs_init(struct super_block *super)
{
	return 0;
}

void reiser4_sysfs_done(struct super_block *super)
{}

int reiser4_sysfs_init_once(void)
{
	return 0;
}

void reiser4_sysfs_done_once(void)
{}

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
