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

	for (orig_id = 1; orig_id < current_num_origins(); orig_id ++)
		for_each_mirror(orig_id, mirr_id)
			if (this == current_origin(mirr_id))
				return 1;
	return 0;
}

static inline int brick_belongs_aid(reiser4_subvol *this)
{
	if (is_meta_brick(this)) {
		if (meta_brick_belongs_aid())
			return 1;
		else
			return 0;
	}
	return data_brick_belongs_volume(this);
}

static inline int brick_id_belongs_aid(u64 id)
{
	if (is_meta_brick_id(id)) {
		if (meta_brick_belongs_aid())
			return 1;
		else
			return 0;
	}
	/*
	 * data brick
	 */
	return id < current_num_origins();
}

/*
 * Returns number of subvolumes participating in AID
 */
static inline u64 num_aid_subvols(reiser4_volume *vol)
{
	if (meta_brick_belongs_aid())
		return vol->num_origins;
	else
		return vol->num_origins - 1;
}

/*
 * Returns matrix of subvolumes participating in AID
 */
static inline reiser4_subvol ***current_aid_subvols(void)
{
	if (meta_brick_belongs_aid())
		return current_subvols();
	else
		return current_subvols() + 1;
}

extern void deactivate_subvol(struct super_block *super, reiser4_subvol *subv);
extern reiser4_subvol *find_meta_brick(reiser4_volume *vol);

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
