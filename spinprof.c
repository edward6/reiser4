/* Copyright 2001, 2002 by Hans Reiser, licensing governed by reiser4/README */

/* spin lock profiling */


#include "kattr.h"
#include "spinprof.h"
#include "debug.h"

#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>

#include <asm/irq.h>
#include <asm/ptrace.h> /* for instruction_pointer() */

#if REISER4_LOCKPROF

#define LEFT(p, buf) (PAGE_SIZE - ((p) - (buf)) - 1)

void profregion_functions_start_here(void);
void profregion_functions_end_here(void);

static locksite none = {
	.hits = 0,
	.func = "",
	.file = "",
	.line = 0
};

struct profregion_attr {
	struct attribute attr;
	ssize_t (*show)(struct profregion *pregion, char *buf);
};

#define PROFREGION_ATTR(aname)			\
static struct profregion_attr aname = {		\
	.attr = {				\
		.name = #aname,			\
		.mode = 0666			\
	},					\
	.show = aname ## _show			\
}

static ssize_t hits_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->hits);
	return (p - buf);
}

static ssize_t busy_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->busy);
	return (p - buf);
}

static ssize_t obj_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%p\n", pregion->obj);
	return (p - buf);
}

static ssize_t objhit_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->objhit);
	return (p - buf);
}

static ssize_t code_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	locksite *site;

	site = pregion->code ? : &none;
	KATTR_PRINT(p, buf, "%s:%s:%i\n", site->func, site->file, site->line);
	return (p - buf);
}

static ssize_t codehit_show(struct profregion *pregion, char *buf)
{
	char *p = buf;
	KATTR_PRINT(p, buf, "%i\n", pregion->codehit);
	return (p - buf);
}

PROFREGION_ATTR(hits);
PROFREGION_ATTR(busy);
PROFREGION_ATTR(obj);
PROFREGION_ATTR(objhit);
PROFREGION_ATTR(code);
PROFREGION_ATTR(codehit);

static ssize_t
profregion_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct profregion *pregion;
	struct profregion_attr *pattr;

	pregion = container_of(kobj, struct profregion, kobj);
	pattr   = container_of(attr, struct profregion_attr, attr);

	return pattr->show(pregion, buf);
}

static ssize_t profregion_store(struct kobject * kobj,struct attribute * attr,
				const char * buf, size_t size)
{
	struct profregion *pregion;

	pregion = container_of(kobj, struct profregion, kobj);
	pregion->hits    = 0;
	pregion->busy    = 0;
	pregion->obj     = 0;
	pregion->objhit  = 0;
	pregion->code    = 0;
	pregion->codehit = 0;
	return size;
}

static struct sysfs_ops profregion_attr_ops = {
	.show  = profregion_show,
	.store = profregion_store
};

static struct attribute * def_attrs[] = {
	&hits.attr, 
	&busy.attr, 
	&obj.attr, 
	&objhit.attr, 
	&code.attr, 
	&codehit.attr, 
	NULL
};

static struct kobj_type ktype_profregion = {
	.sysfs_ops	= &profregion_attr_ops,
	.default_attrs	= def_attrs,
};

static decl_subsys(profregion, &ktype_profregion);

DEFINE_PER_CPU(struct profregionstack, inregion) = {0};
struct profregion outside = {
	.hits = 0,
	.kobj = {
		.name = "outside"
	}
};

struct profregion incontext = {
	.hits = 0,
	.kobj = {
		.name = "incontext"
	}
};

struct profregion overhead = {
	.hits = 0,
	.kobj = {
		.name = "overhead"
	}
};

extern struct profregion pregion_spin_jnode_held;
extern struct profregion pregion_spin_jnode_trying;

static int callback(struct notifier_block *self, unsigned long val, void *p)
{
	struct profregionstack *stack;
	struct pt_regs *regs;
	unsigned long pc;
	int ntop;

	regs = p;
	pc = instruction_pointer(regs);

	if (pc > (unsigned long)profregion_functions_start_here &&
	    pc < (unsigned long)profregion_functions_end_here) {
		overhead.hits ++;
		return 0;
	}

	stack = &get_cpu_var(inregion);
	ntop = stack->top;
	if (unlikely(ntop != 0)) {
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
			if (preg->obj != act->objloc) {
				preg->objhit = hits;
				preg->obj    = act->objloc;
				if (preg->champion != NULL)
					preg->champion(preg);
			}
		}

		hits = 0;
		if (act->codeloc != NULL)
			hits = ++ act->codeloc->hits;
		if (unlikely(hits > preg->codehit)) {
			preg->codehit = hits;
			preg->code    = act->codeloc;
		}
		for (; ntop > 0 ; --ntop) {
			preg = stack->stack[ntop - 1].preg;
			if (preg != NULL)
				++ preg->busy;
		}
	} else if (is_in_reiser4_context())
		incontext.hits ++;
	else
		outside.hits ++;
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

	result = profregion_register(&outside);
	if (result != 0)
		return result;

	result = profregion_register(&incontext);
	if (result != 0)
		return result;

	result = profregion_register(&overhead);
	if (result != 0)
		return result;

	return register_profile_notifier(&profregionnotifier);
}
subsys_initcall(profregion_init);

static void __exit
profregion_exit(void)
{
	profregion_unregister(&overhead);
	profregion_unregister(&incontext);
	profregion_unregister(&outside);
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

void profregion_functions_start_here(void) { }

int profregion_find(struct profregionstack *stack, struct profregion *pregion)
{
	int i;

	for (i = stack->top - 2 ; i >= 0 ; -- i) {
		if (stack->stack[i].preg == pregion) {
			return i;
		}
	}
	BUG();
	return 0;
}

void profregfill(struct pregactivation *act,
		 struct profregion *pregion,
		 void *objloc, void *codeloc)
{
	act->preg    = pregion;
	act->objloc  = objloc;
	act->codeloc = codeloc;
}

void profregion_in(int cpu, struct profregion *pregion,
		   void *objloc, locksite *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	preempt_disable();
	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	BUG_ON(ntop == PROFREGION_MAX_DEPTH);
	profregfill(&stack->stack[ntop], pregion, objloc, codeloc);
	/* put optimization barrier here */
	barrier();
	++ stack->top;
}

void profregion_ex(int cpu, struct profregion *pregion)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	BUG_ON(ntop == 0);
	if(likely(stack->stack[ntop - 1].preg == pregion)) {
		do {
			-- ntop;
		} while (ntop > 0 &&
			 stack->stack[ntop - 1].preg == NULL);
		/* put optimization barrier here */
		barrier();
		stack->top = ntop;
	} else
		stack->stack[profregion_find(stack, pregion)].preg = NULL;
	preempt_enable();
	put_cpu();
}

void profregion_replace(int cpu, struct profregion *pregion, 
			void *objloc, void *codeloc)
{
	struct profregionstack *stack;
	int ntop;

	stack = &per_cpu(inregion, cpu);
	ntop = stack->top;
	BUG_ON(ntop == 0);
	profregfill(&stack->stack[ntop - 1], pregion, objloc, codeloc);
}

void profregion_functions_end_here(void) { }

/* REISER4_LOCKPROF */
#else

#if defined (CONFIG_REISER4_NOOPT)

locksite __hits;
locksite __hits_held;
locksite __fits_trying;

#endif /* CONFIG_REISER4_NOOPT */

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
