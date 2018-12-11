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
 * Returns true, if meta-data subvolume participates in AID.
 * Otherwise, returns false
 */
static inline int meta_brick_belongs_aid(void)
{
	return subvol_is_set(get_meta_subvol(), SUBVOL_HAS_DATA_ROOM);
}

static inline int data_brick_belongs_volume(reiser4_subvol *this)
{
	u64 orig_id, mirr_id;

	for (orig_id = 1; orig_id < current_nr_origins(); orig_id ++)
		for_each_mirror(orig_id, mirr_id)
			if (this == current_origin(mirr_id))
				return 1;
	return 0;
}

static inline int brick_belongs_aid(reiser4_subvol *this)
{
	return is_meta_brick(this) ? meta_brick_belongs_aid() :
		data_brick_belongs_volume(this);
}

/*
 * Returns number of subvolumes participating in AID
 */
static inline u64 num_aid_subvols(reiser4_volume *vol)
{
	if (meta_brick_belongs_aid())
		return vol_nr_origins(vol);
	else
		return vol_nr_origins(vol) - 1;
}

/*
 * Returns matrix of subvolumes participating in AID
 */

static inline slot_t *aid_subvols(slot_t *subvols)
{
	if (meta_brick_belongs_aid())
		return subvols;
	else
		return subvols + 1;
}

static inline slot_t *current_aid_subvols(void)
{
	return aid_subvols(current_subvols());
}

extern void deactivate_subvol(struct super_block *super, reiser4_subvol *subv);
extern reiser4_subvol *find_meta_brick_by_id(reiser4_volume *vol);
extern slot_t alloc_one_mirror_slot(u32 nr_mirrors);
extern slot_t *alloc_mirror_slots(u32 nr_slots);
extern void free_mirror_slot_at(reiser4_volume *vol, u64 idx);
extern void free_mirror_slots(reiser4_subvol ***slots);

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
