/*
  Copyright (c) 2014-2017 Eduard Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef FSW32_H
#define FSW32_H

struct fsw32_aid {
	u64 numb; /* number of abstract buckets */
	u32 nums_bits; /* logarithm of number of segments in the hash space) */
	u32 *tab; /* new system table */
	u32 *weights; /* array of weights */
	u32 *new_weights;
	u32 *shortage;
	u32 *excess;
	bucket_t *buckets; /* set of abstract buckets */
	struct bucket_ops *ops;
};

extern u32 murmur3_x86_32(const char *data, int len, int seed);

extern int initr_fsw32(reiser4_aid *raid, struct bucket_ops *ops,
		       int num_buckets, int nums_bits);
extern void replace_fsw32(reiser4_aid *raid, void **target);
extern void free_fsw32(void *tab);
extern void doner_fsw32(reiser4_aid *raid);

extern int initv_fsw32(bucket_t *buckets,
		       u64 numb, int nums_bits,
		       struct bucket_ops *ops,
		       reiser4_aid *new);
extern void donev_fsw32(reiser4_aid *raid);
extern u64 lookup_fsw32m(reiser4_aid *raid, const char *str,
			 int len, u32 seed, void *tab);
extern int inc_fsw32(reiser4_aid *raid, u64 pos, bucket_t new);
extern int dec_fsw32(reiser4_aid *raid, u64 pos, bucket_t victim);
extern int spl_fsw32(reiser4_aid *raid, u32 fact_bits);
extern void pack_fsw32(reiser4_aid *raid, char *to, u64 src_off, u64 count);
extern void unpack_fsw32(reiser4_aid *raid, char *from, u64 dst_off, u64 count);
extern void dump_fsw32(reiser4_aid *raid, char *to, u64 offset, u32 size);
extern bucket_t *get_buckets_fsw32(reiser4_aid *raid);

#endif /* FSW32_H */

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
