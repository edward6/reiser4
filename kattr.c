/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Interface to sysfs' attributes */

#include "debug.h"
#include "super.h"

#include <linux/kobject.h>     /* struct kobject */
#include <linux/fs.h>          /* struct super_block */

typedef struct reiser4_kattr {
	struct attribute attr;
	ssize_t (*show) (struct super_block * s, char *buf, size_t count,
			 loff_t off);
} reiser4_kattr;

#define DEFINE_REISER4_KATTR(aname, amode)	\
static reiser4_kattr kattr_ ## aname = {	\
	.attr = {				\
		.name = #aname,			\
		.mode = (amode)			\
	},					\
	.show	= show_ ## aname		\
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

#define left (count - (p - buf) - 1)

static ssize_t show_test (struct super_block * s, char *buf, size_t count,
			  loff_t off)
{
	char *p;
	static int cnt = 0;

	p = buf;
	p += snprintf(p, left, "cnt: %i\n", ++ cnt);
	return (p - buf);
}

static ssize_t
kattr_show(struct kobject *kobj, struct attribute *attr,
	   char *buf, size_t count, loff_t off)
{
	struct super_block *super;
	reiser4_kattr *kattr;
	ssize_t ret;

	super = to_super(kobj);
	kattr = to_kattr(attr);

	if (kattr->show != NULL && off == 0)
		ret = kattr->show(super, buf, count, off);
	else
		ret = 0;
	return ret;
}

DEFINE_REISER4_KATTR(test, 0444);

static struct attribute * def_attrs[] = {
	&kattr_test.attr,
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

int reiser4_sysfs_init(struct super_block *super)
{
	reiser4_super_info_data *info;
	struct kobject *kobj;
	int result;

	info = get_super_private(super);

	kobj = &info->kobj;

	snprintf(kobj->name, KOBJ_NAME_LEN, "%s", 
		 kdevname(to_kdev_t(super->s_dev)));
	kobj_set_kset_s(info,reiser4_subsys);
	result = kobject_register(kobj);
	/* here optional attributes may be added */
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
