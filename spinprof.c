/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */


#include "spinprof.h"
#include "debug.h"

#include <linux/percpu.h>
#include <linux/notifier.h>

#include <asm/irq.h>

#ifdef CONFIG_PROFILING

#define LEFT(p, buf) (PAGE_SIZE - ((p) - (buf)) - 1)

static ssize_t
profregion_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct profregion *pregion;
	char *p;

	p = buf;
	pregion = container_of(kobj, struct profregion, kobj);
	p += snprintf(p, LEFT(p, buf), "%i\n", pregion->hits);
	return (p - buf);
}

static struct sysfs_ops profregion_attr_ops = {
	.show = profregion_show
};

static struct attribute hits_attr = {
	.name = "hits",
	.mode = 0444
};

static struct attribute * def_attrs[] = {
	&hits_attr
};

static struct kobj_type ktype_profregion = {
	.sysfs_ops	= &profregion_attr_ops,
	.default_attrs	= def_attrs,
};

static decl_subsys(profregion, &ktype_profregion);

DEFINE_PER_CPU(struct profregionstack, inregion);

static int callback(struct notifier_block *self, unsigned long val, void *p)
{
	struct profregionstack *stack;

	stack = &get_cpu_var(inregion);
	if (stack->top != 0)
		stack->stack[stack->top - 1]->hits ++;
	put_cpu_var(inregion);
	return 0;
}

static struct notifier_block profregionnotifier = {
	.notifier_call = callback
};

/* different architectures tend to declare register_profile_notifier() in
 * different places */
extern int register_profile_notifier(struct notifier_block * nb);

int __init
profregion_init(void)
{
	int result;

	result = subsystem_register(&profregion_subsys);
	if (result != 0)
		return result;

	return register_profile_notifier(&profregionnotifier);
}
subsys_initcall(profregion_init);

static void __exit
profregion_exit(void)
{
	subsystem_unregister(&profregion_subsys);
}
__exitcall(profregion_exit);

int profregion_register(struct profregion *pregion)
{
	kobj_set_kset_s(pregion, profregion_subsys);
	return kobject_register(&pregion->kobj);
}

void profregion_unregister(struct profregion *pregion)
{
	kobject_register(&pregion->kobj);
}

/* CONFIG_PROFILING */
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
