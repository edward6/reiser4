/* Copyright (C) 2001 Hans Reiser.  All rights reserved.
 */

#ifndef __REISER4_ZIPF_H__
#define __REISER4_ZIPF_H__

#include <sys/types.h>

/* Zipf numbers */
/* alpha=6.0 gives ~ 80/20 dist */
/* alpha=1.5 gives ~ 45/20 dist */

#define SYS_RAND_SEED         0x63705UL

typedef struct _zipf_table  zipf_table;

extern void          sys_rand_init          (void);
extern u_int32_t     sys_lrand              (u_int32_t   max);   /* Uniform [0,max) dist. */
extern double        sys_drand              (void);              /* Uniform [0,1) dist. */
extern u_int32_t     sys_erand              (u_int32_t   mean);  /* Exponential random dist. */
extern u_int32_t*    sys_rand_permutation   (u_int32_t   elts);

extern zipf_table*   zipf_compute_table     (u_int32_t   elts,
					     double      alpha);
extern zipf_table*   zipf_permute_table     (u_int32_t   elts,
					     double      alpha);
extern u_int32_t     zipf_choose_cdf        (zipf_table *table,
					     double      prob,
					     double     *cdf);   /* Returns the CDF of prob. */
extern u_int32_t     zipf_choose_elt        (zipf_table *table);

#endif /* __REISER4_ZIPF_H__ */
