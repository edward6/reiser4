/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to sysfs' attributes */

#include "debug.h"
#include "super.h"
#include "kattr.h"

#include <linux/kobject.h>     /* struct kobject */
#include <linux/fs.h>          /* struct super_block */

#define DEFINE_REISER4_KATTR(aname, amode, acookie)	\
static reiser4_kattr kattr_ ## aname = {		\
	.attr = {					\
		.name = #aname,				\
		.mode = (amode)				\
	},						\
	.cookie = acookie,				\
	.show	= show_ ## aname			\
}

static inline reiser4_kattr *
to_kattr(struct attribute *attr)
{
	return container_of(attr, reiser4_kattr, attr);
}

static inline struct super_block *
to_super(struct kobject *kobj)
{
	reiser4_super_info_data *info;

	info = container_of(kobj, reiser4_super_info_data, kobj);
	return info->tree.super;
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
		.name = #afield,				\
		.mode = 0444   /* r--r--r-- */			\
	},							\
	.cookie = &__cookie_ ## aname,				\
	.show = show_ro_ ## asize				\
}

#define getat(ptr, offset, type) *(type *)(((char *)(ptr)) + (offset))
#define LEFT(p, buf) (PAGE_SIZE - (p - buf) - 1)

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
	p += snprintf(p, LEFT(p, buf), cookie->format, (unsigned long long)val);
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
	p += snprintf(p, LEFT(p, buf), cookie->format, (unsigned long long)val);
	return (p - buf);
}

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
	NULL
};

static struct sysfs_ops attr_ops = {
	.show = kattr_show,
};

static struct kobj_type ktype_reiser4 = {
	.sysfs_ops	= &attr_ops,
	.default_attrs	= def_attrs,
};

/* define reiser4_subsys */
static decl_subsys(reiser4, &ktype_reiser4);

#if REISER4_STATS

static ssize_t
kattr_stats_show(struct kobject *kobj, struct attribute *attr,  char *buf)
{
	reiser4_super_info_data *info;
	struct super_block *super;
	reiser4_kattr *kattr;

	info = container_of(kobj, reiser4_super_info_data, stats_kobj);
	kattr = to_kattr(attr);

	if (kattr->show != NULL)
		return kattr->show(info->tree.super, kattr, 0, buf);
	else
		return 0;
}

static struct sysfs_ops stats_attr_ops = {
	.show = kattr_stats_show,
};

static struct kobj_type ktype_noattr = {
	.sysfs_ops	= &stats_attr_ops,
	.default_attrs	= NULL,
};

static ssize_t
kattr_level_show(struct kobject *kobj, struct attribute *attr,  char *buf)
{
	reiser4_super_info_data *info;
	reiser4_level_stats_kobj *level_kobj;
	int level;
	reiser4_kattr *kattr;

	level_kobj = container_of(kobj, reiser4_level_stats_kobj, kobj);
	level = level_kobj->level;
	level_kobj -= level;
	info = container_of(level_kobj, reiser4_super_info_data, level[0]);
	kattr = to_kattr(attr);

	if (kattr->show != NULL)
		return kattr->show(info->tree.super, kattr, &level, buf);
	else
		return 0;
}

static struct sysfs_ops attr_level_ops = {
	.show = kattr_level_show,
};

static struct kobj_type ktype_level_reiser4 = {
	.sysfs_ops	= &attr_level_ops,
	.default_attrs	= def_attrs,
};

static int register_level_attrs(reiser4_super_info_data *info, int i)
{
	struct kobject *parent;
	struct kobject *level;
	int result;

	parent = &info->stats_kobj;
	info->level[i].level = i;
	level = &info->level[i].kobj;
	level->parent = kobject_get(parent);
	if (level->parent != NULL) {
		snprintf(level->name, 
			 KOBJ_NAME_LEN, "level-%2.2i", i + LEAF_LEVEL);
		level->ktype = &ktype_level_reiser4;
		result = kobject_register(level);
		if (result == 0)
			result = reiser4_populate_kattr_level_dir(level, i);
	} else
		result = -EBUSY;
	return result;
}
#endif

int reiser4_sysfs_init(struct super_block *super)
{
	reiser4_super_info_data *info;
	struct kobject *kobj;
	int result;
	ON_STATS(struct kobject *stats_kobj);

	info = get_super_private(super);

	kobj = &info->kobj;

	snprintf(kobj->name, KOBJ_NAME_LEN, "%s", 
		 kdevname(to_kdev_t(super->s_dev)));
	kobj_set_kset_s(info, reiser4_subsys);
	result = kobject_register(kobj);
	if (result != 0)
		return result;
#if REISER4_STATS
	/* add attributes representing statistical counters */
	stats_kobj = &info->stats_kobj;
	stats_kobj->parent = kobject_get(kobj);
	snprintf(stats_kobj->name, KOBJ_NAME_LEN, "stats");
	stats_kobj->ktype = &ktype_noattr;
	result = kobject_register(stats_kobj);
	if (result != 0)
		return result;
	result = reiser4_populate_kattr_dir(stats_kobj);
	if (result == 0) {
		int i;

		for (i = 0; i < REAL_MAX_ZTREE_HEIGHT; ++i) {
			result = register_level_attrs(info, i);
			if (result != 0)
				break;
		}
	}
#endif
	return result;
}

void reiser4_sysfs_done(struct super_block *super)
{
	kobject_unregister(&get_super_private(super)->kobj);
}

int reiser4_sysfs_init_all(void)
{
	return subsystem_register(&reiser4_subsys);
}

void reiser4_sysfs_done_all(void)
{
	subsystem_unregister(&reiser4_subsys);
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
