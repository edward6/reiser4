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
};

extern u32 murmur3_x86_32(const char *data, int len, int seed);

extern int initr_fsx32(reiser4_dcx *rdcx, void **tab, int nums_bits);
extern reiser4_subvol *dst_builtin(const struct inode *inode, loff_t offset);
extern void replace_fsx32(reiser4_dcx *rdcx, void **target);
extern void free_fsx32(void *tab);
extern void doner_fsx32(void **tab);
extern int initv_fsx32(void **tab, u64 numb, int nums_bits, reiser4_dcx *rdcx);
extern void donev_fsx32(reiser4_dcx *rdcx);
extern u64 lookup_fsx32m(reiser4_dcx *rdcx, const struct inode *inode,
			 const char *str, int len, u32 seed, void *tab);
extern int inc_fsx32(reiser4_dcx *rdcx, const void *tab, u64 pos, bucket_t new);
extern int dec_fsx32(reiser4_dcx *rdcx, const void *tab, u64 pos, bucket_t victim);
extern int spl_fsx32(reiser4_dcx *rdcx, const void *tab, u32 fact_bits);
extern void pack_fsx32(reiser4_dcx *rdcx, char *to, u64 src_off, u64 count);
extern void unpack_fsx32(reiser4_dcx *rdcx, void *tab,
			 char *from, u64 dst_off, u64 count);
extern void dump_fsx32(reiser4_dcx *rdcx, void *tab,
		       char *to, u64 offset, u32 size);
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
