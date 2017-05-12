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
	u64 numb; /* number of buckets */
	u32 nums_bits; /* logarithm of number of segments in the hash space) */
	u32 *tab; /* system table */
	u32 *weights; /* array of weights */
	void *buckets; /* array of abstract buckets, i.e. the AID itself */
	struct reiser4_aid_ops *ops;
};

extern u32 murmur3_x86_32(const char *data, int len, int seed);

extern int fsw32_init(void *buckets,
		      u64 numb, int nums_bits,
		      struct reiser4_aid_ops *ops,
		      reiser4_aid **new);
extern void fsw32_done(reiser4_aid *aid);
extern u64 fsw32m_lookup_bucket(reiser4_aid *aid, const char *str,
				int len, u32 seed);
extern int fsw32_add_bucket(reiser4_aid *aid, void *bucket);
extern int fsw32_remove_bucket(reiser4_aid *aid, u64 victim_pos);
extern int fsw32_split(reiser4_aid *aid, u32 fact_bits);
extern void tab32_pack(char *to, void *from, u64 count);
extern void tab32_unpack(void *to, char *from, u64 count);

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
