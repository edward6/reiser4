/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Wrapper functions/macros for spinlocks. */

#ifndef __SPIN_MACROS_H__
#define __SPIN_MACROS_H__

#include "debug.h"

#if REISER4_USER_LEVEL_SIMULATION
#    define check_spin_is_locked(s)     spin_is_locked(s)
#    define check_spin_is_not_locked(s) spin_is_not_locked(s)
#elif defined( CONFIG_DEBUG_SPINLOCK ) && defined( CONFIG_SMP )
#    define check_spin_is_not_locked(s) ( ( s ) -> owner != get_current() )
#    define spin_is_not_locked(s)       ( ( s ) -> owner == NULL )
#    define check_spin_is_locked(s)     ( ( s ) -> owner == get_current() )
#else
#    define check_spin_is_not_locked(s) (1)
#    define spin_is_not_locked(s)       (1)
#    if defined( CONFIG_SMP )
#        define check_spin_is_locked(s)     spin_is_locked(s)
#    else
#        define check_spin_is_locked(s)     (1)
#    endif
#endif

/* Define several inline functions for each type of spinlock. */
#define SPIN_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
static inline void spin_ ## NAME ## _inc(void)   				\
{										\
	ON_DEBUG_CONTEXT(++ lock_counters()->spin_locked_ ## NAME);		\
	ON_DEBUG_CONTEXT(++ lock_counters()->spin_locked);			\
}										\
										\
static inline void spin_ ## NAME ## _dec(void)   				\
{										\
	ON_DEBUG_CONTEXT(--lock_counters()->spin_locked_ ## NAME);		\
	ON_DEBUG_CONTEXT(--lock_counters()->spin_locked);			\
}										\
										\
static inline int  spin_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return check_spin_is_locked (& x->FIELD);				\
}										\
										\
static inline int  spin_ ## NAME ## _is_not_locked (TYPE *x)			\
{										\
	return check_spin_is_not_locked (& x->FIELD);				\
}										\
										\
static inline void spin_lock_ ## NAME ## _no_ord (TYPE *x)			\
{										\
	assert( "nikita-2703", spin_ ## NAME ## _is_not_locked( x ) );		\
	spin_lock( &x -> FIELD );						\
	spin_ ## NAME ## _inc();        					\
}										\
										\
static inline void spin_lock_ ## NAME (TYPE *x)					\
{										\
	ON_DEBUG_CONTEXT( assert( "nikita-1383",				\
				  spin_ordering_pred_ ## NAME( x ) ) );		\
	spin_lock_ ## NAME ## _no_ord( x );					\
}										\
										\
static inline int  spin_trylock_ ## NAME (TYPE *x)				\
{										\
	if (spin_trylock (& x->FIELD)) {					\
		spin_ ## NAME ## _inc();    					\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
static inline void spin_unlock_ ## NAME (TYPE *x)				\
{										\
	ON_DEBUG_CONTEXT( assert( "nikita-1375",				\
		lock_counters() -> spin_locked_ ## NAME > 0 ) );		\
	ON_DEBUG_CONTEXT( assert( "nikita-1376",				\
		lock_counters() -> spin_locked > 0 ) );				\
	spin_ ## NAME ## _dec();        					\
	assert( "nikita-2703", spin_ ## NAME ## _is_locked( x ) );		\
	spin_unlock (& x->FIELD);						\
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

#define UNDER_SPIN( obj_type, obj, exp )	\
({						\
	typeof ( obj ) __obj;			\
	typeof ( exp ) __result;		\
						\
	__obj = ( obj );			\
	assert( "nikita-2492", __obj != NULL );	\
	spin_lock_ ## obj_type ( __obj );	\
	__result = exp;				\
	spin_unlock_ ## obj_type ( __obj );	\
	__result;				\
})

#define UNDER_SPIN_VOID( obj_type, obj, exp )	\
({						\
	typeof ( obj ) __obj;			\
						\
	__obj = ( obj );			\
	assert( "nikita-2492", __obj != NULL );	\
	spin_lock_ ## obj_type ( __obj );	\
	exp;					\
	spin_unlock_ ## obj_type ( __obj );	\
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
