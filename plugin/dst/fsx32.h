/*
  Copyright (c) 2014-2019 Eduard Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef FSX32_H
#define FSX32_H

struct fsx32_dcx {
	u64 numb; /* number of abstract buckets */
	u32 nums_bits; /* logarithm of number of hash space segments */
	u32 *tab; /* system table */
	u32 *weights; /* array of weights */
	u32 *new_weights;
	u32 *sho;
	u32 *exc;
	bucket_t *buckets; /* set of abstract buckets */
	struct bucket_ops *ops;
};

extern u32 murmur3_x86_32(const char *data, int len, int seed);

extern int initr_fsx32(reiser4_dcx *rdcx, void **tab,
		       int num_buckets, int nums_bits);
extern void replace_fsx32(reiser4_dcx *rdcx, void **target);
extern void free_fsx32(void *tab);
extern void doner_fsx32(void **tab);

extern void init_lite_fsx32(bucket_t *vec, struct bucket_ops *ops,
			    reiser4_dcx *rdcx);
extern int initv_fsx32(bucket_t *buckets, void **tab,
		       u64 numb, int nums_bits,
		       struct bucket_ops *ops,
		       reiser4_dcx *new);
extern void donev_fsx32(reiser4_dcx *rdcx);
extern u64 lookup_fsx32m(reiser4_dcx *rdcx, const char *str,
			 int len, u32 seed, void *tab);
extern int inc_fsx32(reiser4_dcx *rdcx, void *tab, u64 pos, bucket_t new);
extern int dec_fsx32(reiser4_dcx *rdcx, void *tab, u64 pos, bucket_t victim);
extern int spl_fsx32(reiser4_dcx *rdcx, u32 fact_bits);
extern void pack_fsx32(reiser4_dcx *rdcx, char *to, u64 src_off, u64 count);
extern void unpack_fsx32(reiser4_dcx *rdcx, void *tab,
			 char *from, u64 dst_off, u64 count);
extern void dump_fsx32(reiser4_dcx *rdcx, void *tab,
		       char *to, u64 offset, u32 size);
extern bucket_t *get_buckets_fsx32(reiser4_dcx *rdcx);

#endif /* FSX32_H */

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 80
  scroll-step: 1
  End:
*/
