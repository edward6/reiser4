/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */


#include "spinprof.h"
#include "debug.h"

#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>

#include <asm/irq.h>

#if REISER4_LOCKPROF

#define LEFT(p, buf) (PAGE_SIZE - ((p) - (buf)) - 1)

static locksite none = {
	.hits = 0,
	.func = "<noop>",
	.file = "<nowhere>",
	.line = 0
};

static ssize_t
profregion_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct profregion *pregion;
	char *p;
	locksite *site;

	p = buf;
	pregion = container_of(kobj, struct profregion, kobj);

	site = pregion->code ? : &none;
	p += snprintf(p, LEFT(p, buf), "%i %p [%i] %s:%s:%i [%i]\n",
		      pregion->hits, pregion->obj, pregion->objhit,
		      site->func, site->file, site->line, pregion->codehit);
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

DEFINE_PER_CPU(struct profregionstack, inregion) = {0};

extern struct profregion pregion_spin_jnode_held;
extern struct profregion pregion_spin_jnode_trying;

static int callback(struct notifier_block *self, unsigned long val, void *p)
{
	struct profregionstack *stack;
	int ntop;

	stack = &get_cpu_var(inregion);
	ntop = atomic_read(&stack->top);
	if (ntop != 0) {
		struct pregactivation *act;
		struct profregion *preg;
		int hits;

		act = &stack->stack[ntop - 1];
		preg = act->preg;
		preg->hits ++;

		hits = 0;
		if (act->objloc != NULL) {
			BUG_ON(*act->objloc == 0x6b6b6b6b);
			BUG_ON(*act->objloc == 0x5a5a5a5a);
			hits = ++ (*act->objloc);
		}
		if (unlikely(hits > preg->objhit)) {
			preg->objhit = hits;
			preg->obj    = act->objloc;
		}

		hits = 0;
		if (act->codeloc != NULL)
			hits = ++ act->codeloc->hits;
		if (unlikely(hits > preg->codehit)) {
			preg->codehit = hits;
			preg->code    = act->codeloc;
		}
	}
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

int profregion_find(struct profregionstack *stack, struct profregion *pregion)
{
	int i;

	for (i = atomic_read(&stack->top) - 2 ; i >= 0 ; -- i) {
		if (stack->stack[i].preg == pregion) {
			return i;
		}
	}
	BUG();
	return 0;
}

/* REISER4_LOCKPROF */
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
