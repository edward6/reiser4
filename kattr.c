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

static struct attribute * def_attrs[] = {
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

static int register_level_attrs(struct kobject *parent, 
				reiser4_super_info_data *info, int i)
{
	struct kobject *level;
	int result;

	info->level[i].level = i;
	level = &info->level[i].kobj;
	level->parent = kobject_get(parent);
	if (level->parent != NULL) {
		snprintf(level->name, 
			 KOBJ_NAME_LEN, "level%2.2i", i + LEAF_LEVEL);
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

	info = get_super_private(super);

	kobj = &info->kobj;

	snprintf(kobj->name, KOBJ_NAME_LEN, "%s", 
		 kdevname(to_kdev_t(super->s_dev)));
	kobj_set_kset_s(info, reiser4_subsys);
	result = kobject_register(kobj);
#if REISER4_STATS
	/* add attributes representing statistical counters */
	if (result == 0) {
		result = reiser4_populate_kattr_dir(kobj);
		if (result == 0) {
			int i;

			for (i = 0; i < REAL_MAX_ZTREE_HEIGHT; ++i) {
				result = register_level_attrs(kobj, info, i);
				if (result != 0)
					break;
			}
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
