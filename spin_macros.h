/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Wrapper functions/macros for spinlocks. */

#ifndef __SPIN_MACROS_H__
#define __SPIN_MACROS_H__

#include <linux/spinlock.h>
#include <linux/profile.h>

#include "debug.h"
#include "spinprof.h"

/* not yet implemented */
#define check_is_write_locked(s)     (1)
#define check_is_read_locked(s)      (1)
#define check_is_not_read_locked(s)  (1)
#define check_is_not_write_locked(s) (1)

#if REISER4_USER_LEVEL_SIMULATION
#    define check_spin_is_locked(s)     spin_is_locked(s)
#    define check_spin_is_not_locked(s) spin_is_not_locked(s)
#elif defined(CONFIG_DEBUG_SPINLOCK) && defined(CONFIG_SMP) && 0
#    define check_spin_is_not_locked(s) ((s)->owner != get_current())
#    define spin_is_not_locked(s)       ((s)->owner == NULL)
#    define check_spin_is_locked(s)     ((s)->owner == get_current())
#else
#    define check_spin_is_not_locked(s) (1)
#    define spin_is_not_locked(s)       (1)
#    if defined(CONFIG_SMP)
#        define check_spin_is_locked(s)     spin_is_locked(s)
#    else
#        define check_spin_is_locked(s)     (1)
#    endif
#endif

#define __ODC ON_DEBUG_CONTEXT
#define __ODCA(l, e) __ODC(assert(l, e))

#if REISER4_LOCKPROF

#define DEFINE_SPIN_PROFREGIONS(aname)						\
struct profregion pregion_spin_ ## aname ## _held = {				\
	.kobj = {								\
		.name = #aname  "_h"						\
	}									\
};										\
										\
struct profregion pregion_spin_ ## aname ## _trying = {				\
	.kobj = {								\
		.name = #aname  "_t"						\
	}									\
};										\
										\
static inline int register_ ## aname ## _profregion(void)			\
{										\
	int result;								\
										\
	result = profregion_register(&pregion_spin_ ## aname ## _held);		\
	if (result != 0)							\
		return result;							\
	result = profregion_register(&pregion_spin_ ## aname ## _trying);	\
	return result;								\
}										\
										\
static inline void unregister_ ## aname ## _profregion(void)			\
{										\
	profregion_unregister(&pregion_spin_ ## aname ## _held);		\
	profregion_unregister(&pregion_spin_ ## aname ## _trying);		\
}										\
										\
typedef struct { int foo; } aname ## _spin_dummy_profregion

#define DECLARE_SPIN_PROFREGIONS(NAME)				\
extern struct profregion pregion_spin_ ## NAME ## _held;	\
extern struct profregion pregion_spin_ ## NAME ## _trying;


#define DEFINE_RW_PROFREGIONS(aname)						\
struct profregion pregion_rw_ ## aname ## _r_held = {				\
	.kobj = {								\
		.name = #aname  "_r_h"						\
	}									\
};										\
										\
struct profregion pregion_rw_ ## aname ## _w_held = {				\
	.kobj = {								\
		.name = #aname  "_w_h"						\
	}									\
};										\
										\
struct profregion pregion_rw_ ## aname ## _r_trying = {				\
	.kobj = {								\
		.name = #aname  "_r_t"						\
	}									\
};										\
										\
struct profregion pregion_rw_ ## aname ## _w_trying = {				\
	.kobj = {								\
		.name = #aname  "_w_t"						\
	}									\
};										\
										\
static inline int register_ ## aname ## _profregion(void)			\
{										\
	int result;								\
										\
	result = profregion_register(&pregion_rw_ ## aname ## _r_held);		\
	if (result != 0)							\
		return result;							\
	result = profregion_register(&pregion_rw_ ## aname ## _w_held);		\
	if (result != 0)							\
		return result;							\
	result = profregion_register(&pregion_rw_ ## aname ## _r_trying);	\
	if (result != 0)							\
		return result;							\
	result = profregion_register(&pregion_rw_ ## aname ## _w_trying);	\
	return result;								\
}										\
										\
static inline void unregister_ ## aname ## _profregion(void)			\
{										\
	profregion_unregister(&pregion_rw_ ## aname ## _r_held);		\
	profregion_unregister(&pregion_rw_ ## aname ## _w_held);		\
	profregion_unregister(&pregion_rw_ ## aname ## _r_trying);		\
	profregion_unregister(&pregion_rw_ ## aname ## _w_trying);		\
}										\
										\
typedef struct { int foo; } aname ## _rw_dummy_profregion

#define DECLARE_RW_PROFREGIONS(NAME)				\
extern struct profregion pregion_rw_ ## NAME ## _r_held;	\
extern struct profregion pregion_rw_ ## NAME ## _w_held;	\
extern struct profregion pregion_rw_ ## NAME ## _r_trying;	\
extern struct profregion pregion_rw_ ## NAME ## _w_trying;

#define GETCPU(cpu)				\
	int cpu = get_cpu()

#define PUTCPU(cpu) put_cpu()

#define PREG_IN(cpu, preg, objloc, codeloc)				\
	profregion_in(cpu, preg, objloc, codeloc)

#define PREG_REPLACE(cpu, preg, objloc, codeloc)			\
	profregion_replace(cpu, preg, objloc, codeloc)

#define PREG_EX(cpu, preg) profregion_ex(cpu, preg)

/* REISER4_LOCKPROF */
#else

#define DEFINE_SPIN_PROFREGIONS(aname)				\
static inline int register_ ## aname ## _profregion(void)	\
{								\
	return 0;						\
}								\
								\
static inline void unregister_ ## aname ## _profregion(void)	\
{								\
}

#define DECLARE_SPIN_PROFREGIONS(NAME)

#define DEFINE_RW_PROFREGIONS(aname)				\
static inline int register_ ## aname ## _profregion(void)	\
{								\
	return 0;						\
}								\
								\
static inline void unregister_ ## aname ## _profregion(void)	\
{								\
}

#define DECLARE_RW_PROFREGIONS(NAME)

#define GETCPU(cpu)
#define PUTCPU(cpu)
#define PREG_IN(cpu, preg, objloc, codeloc)
#define PREG_REPLACE(cpu, preg, objloc, codeloc)
#define PREG_EX(cpu, preg)

/* REISER4_LOCKPROF */
#endif

typedef struct reiser4_spin_data {
	spinlock_t lock;
#if REISER4_LOCKPROF
	int        held;
	int        trying;
#endif
} reiser4_spin_data;

typedef struct reiser4_rw_data {
	rwlock_t lock;
#if REISER4_LOCKPROF
	int      r_held;
	int      r_trying;
	int      w_held;
	int      w_trying;
#endif
} reiser4_rw_data;

/* Define several inline functions for each type of spinlock. */
#define SPIN_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
DECLARE_SPIN_PROFREGIONS(NAME)							\
										\
static inline void spin_ ## NAME ## _init(TYPE *x)				\
{										\
	assert("nikita-2987", x != NULL);					\
	memset(& x->FIELD, 0, sizeof x->FIELD); 				\
	spin_lock_init(& x->FIELD.lock);					\
}										\
										\
static inline void spin_ ## NAME ## _inc(void)					\
{										\
	__ODC(++ lock_counters()->spin_locked_ ## NAME);			\
	__ODC(++ lock_counters()->spin_locked);					\
}										\
										\
static inline void spin_ ## NAME ## _dec(void)					\
{										\
	__ODC(--lock_counters()->spin_locked_ ## NAME);				\
	__ODC(--lock_counters()->spin_locked);					\
}										\
										\
static inline int  spin_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return check_spin_is_locked (& x->FIELD.lock);				\
}										\
										\
static inline int  spin_ ## NAME ## _is_not_locked (TYPE *x)			\
{										\
	return check_spin_is_not_locked (& x->FIELD.lock);			\
}										\
										\
static inline void spin_lock_ ## NAME ## _no_ord (TYPE *x, locksite *loc)	\
{										\
	GETCPU(cpu);								\
	assert("nikita-2703", spin_ ## NAME ## _is_not_locked(x));		\
	PREG_IN(cpu, &pregion_spin_ ## NAME ## _trying, &x->FIELD.trying, loc);	\
	spin_lock(&x->FIELD.lock);						\
	PREG_REPLACE(cpu,							\
		     &pregion_spin_ ## NAME ## _held, &x->FIELD.held, loc);	\
	PUTCPU(cpu);								\
	spin_ ## NAME ## _inc();						\
}										\
										\
static inline void spin_lock_ ## NAME ## _at (TYPE *x, locksite *loc)		\
{										\
	__ODCA("nikita-1383", spin_ordering_pred_ ## NAME(x));			\
	spin_lock_ ## NAME ## _no_ord(x, loc);					\
}										\
										\
static inline void spin_lock_ ## NAME (TYPE *x)					\
{										\
	__ODCA("nikita-1383", spin_ordering_pred_ ## NAME(x));			\
	spin_lock_ ## NAME ## _no_ord(x, NULL);					\
}										\
										\
static inline int  spin_trylock_ ## NAME (TYPE *x)				\
{										\
	if (spin_trylock (& x->FIELD.lock)) {					\
		GETCPU(cpu);							\
		spin_ ## NAME ## _inc();					\
		PREG_IN(cpu,							\
			&pregion_spin_ ## NAME ## _held, &x->FIELD.held, 0);	\
		PUTCPU(cpu);							\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
static inline void spin_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-1375", lock_counters()->spin_locked_ ## NAME > 0);	\
	__ODCA("nikita-1376", lock_counters()->spin_locked > 0);		\
	spin_ ## NAME ## _dec();						\
	assert("nikita-2703", spin_ ## NAME ## _is_locked(x));			\
	spin_unlock (& x->FIELD.lock);						\
	PREG_EX(get_cpu(), &pregion_spin_ ## NAME ## _held);			\
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

#define UNDER_SPIN(obj_type, obj, exp)			\
({							\
	typeof (obj) __obj;				\
	typeof (exp) __result;				\
	LOCKSITE_INIT(__hits);				\
							\
	__obj = (obj);					\
	assert("nikita-2492", __obj != NULL);		\
	spin_lock_ ## obj_type ## _at (__obj, &__hits);	\
	__result = exp;					\
	spin_unlock_ ## obj_type (__obj);		\
	__result;					\
})

#define UNDER_SPIN_VOID(obj_type, obj, exp)		\
({							\
	typeof (obj) __obj;				\
	LOCKSITE_INIT(__hits);				\
							\
	__obj = (obj);					\
	assert("nikita-2492", __obj != NULL);		\
	spin_lock_ ## obj_type ## _at (__obj, &__hits);	\
	exp;						\
	spin_unlock_ ## obj_type (__obj);		\
})


/* Define several inline functions for each type of rwlock. */
#define RW_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
DECLARE_RW_PROFREGIONS(NAME)							\
										\
static inline void rw_ ## NAME ## _init(TYPE *x)				\
{										\
	assert("nikita-2988", x != NULL);					\
	memset(& x->FIELD, 0, sizeof x->FIELD); 				\
	rwlock_init(& x->FIELD.lock);						\
}										\
										\
static inline int  rw_ ## NAME ## _is_read_locked (const TYPE *x)		\
{										\
	return check_is_read_locked (& x->FIELD.lock);				\
}										\
										\
static inline int  rw_ ## NAME ## _is_write_locked (const TYPE *x)		\
{										\
	return check_is_write_locked (& x->FIELD.lock);				\
}										\
										\
static inline int  rw_ ## NAME ## _is_not_read_locked (TYPE *x)			\
{										\
	return check_is_not_read_locked (& x->FIELD.lock);			\
}										\
										\
static inline int  rw_ ## NAME ## _is_not_write_locked (TYPE *x)		\
{										\
	return check_is_not_write_locked (& x->FIELD.lock);			\
}										\
										\
static inline int  rw_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return check_is_read_locked (& x->FIELD.lock) &&			\
	       check_is_write_locked (& x->FIELD.lock);				\
}										\
										\
static inline int  rw_ ## NAME ## _is_not_locked (const TYPE *x)		\
{										\
	return check_is_not_read_locked (& x->FIELD.lock) &&			\
	       check_is_not_write_locked (& x->FIELD.lock);			\
}										\
										\
static inline void read_ ## NAME ## _inc(void)					\
{										\
	__ODC(++ lock_counters()->read_locked_ ## NAME);			\
	__ODC(++ lock_counters()->rw_locked_ ## NAME);				\
	__ODC(++ lock_counters()->spin_locked);					\
}										\
										\
static inline void read_ ## NAME ## _dec(void)					\
{										\
	__ODC(-- lock_counters()->read_locked_ ## NAME);			\
	__ODC(-- lock_counters()->rw_locked_ ## NAME);				\
	__ODC(-- lock_counters()->spin_locked);					\
}										\
										\
static inline void write_ ## NAME ## _inc(void)					\
{										\
	__ODC(++ lock_counters()->write_locked_ ## NAME);			\
	__ODC(++ lock_counters()->rw_locked_ ## NAME);				\
	__ODC(++ lock_counters()->spin_locked);					\
}										\
										\
static inline void write_ ## NAME ## _dec(void)					\
{										\
	__ODC(-- lock_counters()->write_locked_ ## NAME);			\
	__ODC(-- lock_counters()->rw_locked_ ## NAME);				\
	__ODC(-- lock_counters()->spin_locked);					\
}										\
										\
										\
static inline void read_lock_ ## NAME ## _no_ord (TYPE *x)			\
{										\
	GETCPU(cpu);								\
	assert("nikita-2976", rw_ ## NAME ## _is_not_read_locked(x));		\
	PREG_IN(cpu, &pregion_rw_ ## NAME ## _r_trying, &x->FIELD.r_trying, 0);	\
	read_lock(&x->FIELD.lock);						\
	PREG_REPLACE(cpu, &pregion_rw_ ## NAME ## _r_held, &x->FIELD.r_held, 0);\
	PUTCPU(cpu);								\
	read_ ## NAME ## _inc();						\
}										\
										\
static inline void write_lock_ ## NAME ## _no_ord (TYPE *x)			\
{										\
	GETCPU(cpu);								\
	assert("nikita-2977", rw_ ## NAME ## _is_not_write_locked(x));		\
	PREG_IN(cpu, &pregion_rw_ ## NAME ## _w_trying, &x->FIELD.w_trying, 0);	\
	write_lock(&x->FIELD.lock);						\
	PREG_REPLACE(cpu, &pregion_rw_ ## NAME ## _w_held, &x->FIELD.w_held, 0);\
	PUTCPU(cpu);								\
	write_ ## NAME ## _inc();						\
}										\
										\
static inline void read_lock_ ## NAME (TYPE *x)					\
{										\
	__ODCA("nikita-2975", rw_ordering_pred_ ## NAME(x));			\
	read_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
static inline void write_lock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2978", rw_ordering_pred_ ## NAME(x));			\
	write_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
static inline void read_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2979", lock_counters()->read_locked_ ## NAME > 0);	\
	__ODCA("nikita-2980", lock_counters()->rw_locked_ ## NAME > 0);		\
	__ODCA("nikita-2980", lock_counters()->spin_locked > 0);		\
	read_ ## NAME ## _dec();						\
	assert("nikita-2703", rw_ ## NAME ## _is_read_locked(x));		\
	read_unlock (& x->FIELD.lock);						\
	PREG_EX(get_cpu(), &pregion_rw_ ## NAME ## _r_held);			\
}										\
										\
static inline void write_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2979", lock_counters()->write_locked_ ## NAME > 0);	\
	__ODCA("nikita-2980", lock_counters()->rw_locked_ ## NAME > 0);		\
	__ODCA("nikita-2980", lock_counters()->spin_locked > 0);		\
	write_ ## NAME ## _dec();						\
	assert("nikita-2703", rw_ ## NAME ## _is_write_locked(x));		\
	write_unlock (& x->FIELD.lock);						\
	PREG_EX(get_cpu(), &pregion_rw_ ## NAME ## _w_held);			\
}										\
										\
										\
static inline int  write_trylock_ ## NAME (TYPE *x)				\
{										\
	if (write_trylock (& x->FIELD.lock)) {					\
		GETCPU(cpu);							\
		PREG_IN(cpu, &pregion_rw_ ## NAME ## _w_held,			\
			&x->FIELD.w_held, 0);					\
		PUTCPU(cpu);							\
		write_ ## NAME ## _inc();					\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
										\
typedef struct { int foo; } NAME ## _rw_dummy

#define UNDER_RW(obj_type, obj, rw, exp)	\
({						\
	typeof (obj) __obj;			\
	typeof (exp) __result;			\
						\
	__obj = (obj);				\
	assert("nikita-2981", __obj != NULL);	\
	rw ## _lock_ ## obj_type (__obj);	\
	__result = exp;				\
	rw ## _unlock_ ## obj_type (__obj);	\
	__result;				\
})

#define UNDER_RW_VOID(obj_type, obj, rw, exp)	\
({						\
	typeof (obj) __obj;			\
						\
	__obj = (obj);				\
	assert("nikita-2982", __obj != NULL);	\
	rw ## _lock_ ## obj_type (__obj);	\
	exp;					\
	rw ## _unlock_ ## obj_type (__obj);	\
})

/* __SPIN_MACROS_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
