/* Copyright 2002 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of scalable integers. See scint.c for comments. */

#ifndef __SCINT_H__
#define __SCINT_H__

/* for __u?? types */
#include <linux/types.h>

#if BITS_PER_LONG < 64
typedef struct scint {
	__s32 datum;
} scint_t;

extern int scint_init_once(void);
extern void scint_done_once(void);

extern void scint_init(scint_t *scint);
extern void scint_done(scint_t *scint);

extern int scint_pack(scint_t *scint, __u64 value, int gfp_mask);
extern __u64 scint_unpack(const scint_t *scint);

#else

typedef __u64 scint_t;

static inline int
scint_init_once(void)
{ return 0;}

static inline void
scint_done_once(void)
{;}

static inline void
scint_init(scint_t *scint)
{
	*scint = (__u64)0;
}

static inline void
scint_done(scint_t *scint)
{;}

static inline int
scint_pack(scint_t *scint, __u64 value, int gfp_mask UNUSED_ARG)
{
	*scint = value;
	return 0;
}

static inline __u64
scint_unpack(const scint_t *scint)
{
	return *scint;
}

#endif

/* __SCINT_H__ */
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
