/*
  Copyright (c) 2014-2017 Eduard Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef VOLUME_H
#define VOLUME_H

#define INVALID_SUBVOL_ID   (0xffffffff)
#define METADATA_SUBVOL_ID  (0)

extern void deactivate_subvol(struct super_block *super, reiser4_subvol *subv);
extern reiser4_subvol *find_meta_brick_by_id(reiser4_volume *vol);
extern lv_conf *alloc_lv_conf(u32 nr_slots);
extern void free_lv_conf(lv_conf *conf);
extern void release_volinfo_nodes(reiser4_volinfo *vinfo, int dealloc);
extern slot_t alloc_mslot(u32 nr_mirrors);
extern void free_mslot(slot_t slot);
extern void free_mslot_at(lv_conf *conf, u64 idx);
extern int brick_belongs_volume(reiser4_volume *vol, reiser4_subvol *subv);
extern int remove_brick_tail_asym(reiser4_volume *vol, reiser4_subvol *subv);

static inline int is_meta_brick_id(u64 id)
{
	return id == METADATA_SUBVOL_ID;
}

static inline int is_meta_brick(reiser4_subvol *this)
{
	assert("edward-2189", subvol_is_set(this, SUBVOL_ACTIVATED));
	assert("edward-2071", ergo(is_meta_brick_id(this->id),
				   this == get_meta_subvol()));
	return is_meta_brick_id(this->id);
}

/*
 * Returns true, if meta-data subvolume participates in DSA.
 * Otherwise, returns false
 */
static inline int meta_brick_belongs_dsa(void)
{
	return subvol_is_set(get_meta_subvol(), SUBVOL_HAS_DATA_ROOM);
}

static inline int brick_belongs_dsa(reiser4_volume *vol, reiser4_subvol *this)
{
	return is_meta_brick(this) ? meta_brick_belongs_dsa() :
		brick_belongs_volume(vol, this);
}

/*
 * Returns number of subvolumes participating in DSA
 */
static inline u64 num_dsa_subvols(reiser4_volume *vol)
{
	if (meta_brick_belongs_dsa())
		return vol_nr_origins(vol);
	else
		return vol_nr_origins(vol) - 1;
}

static inline reiser4_subvol *subvol_by_key(const reiser4_key *key)
{
	return current_origin(get_key_ordering(key));
}

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

#endif /* VOLUME_H */
