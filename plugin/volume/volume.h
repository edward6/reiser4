/*
  Copyright (c) 2014-2020 Eduard O. Shishkin

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
extern reiser4_block_nr estimate_migration_iter(void);

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

static inline u64 get_pos_in_dsa(reiser4_subvol *subv)
{
	return subv->dsa_idx;
}

static inline int is_proxy_brick(reiser4_subvol *subv)
{
	return subvol_is_set(subv, SUBVOL_IS_PROXY);
}

/*
 * Returns true, if @subv participates in regular data distribution
 */
static inline int is_dsa_brick(reiser4_subvol *subv)
{
	assert("edward-2475",
	       ergo(is_proxy_brick(subv),
		    !subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM)));
	assert("edward-2476",
	       ergo(subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM),
		    !is_proxy_brick(subv)));

	return subvol_is_set(subv, SUBVOL_HAS_DATA_ROOM);
}

/*
 * Returns true, if meta-data subvolume participates in regular
 * data distribution. Otherwise, returns false
 */
static inline int meta_brick_belongs_dsa(void)
{
	return is_dsa_brick(get_meta_subvol());
}

/**
 * Without scanning all bricks i the volume, calculate and return number
 * of bricks participating in regular data distribution
 *
 * Possible cases:
 *
 * 1 xxxxxxxxxx        nr_origins
 *
 * 2 oxxxxxxxxx        nr_origins - 1
 *
 * 3 xxxxxxxxxx        nr_origins - 1
 *   ^
 * 4 oxxxxxxxxx        nr_origins - 1
 *   ^
 * 5 xxxxxxxxxx        nr_origins - 1
 *      ^
 * 6 oxxxxxxxxx        nr_origins - 2
 *      ^
 *
 * Legend:
 *
 * o:  meta-brick w/o data room
 * x:  data brick, or meta-brick w/ data room
 * ^:  proxy brick
 */
static inline u64 num_dsa_subvols(reiser4_volume *vol)
{
	if (!reiser4_is_set(reiser4_get_current_sb(), REISER4_PROXY_ENABLED))
		/* 1, 2 */
		return meta_brick_belongs_dsa() ?
			vol_nr_origins(vol) : vol_nr_origins(vol) - 1;

	if (subvol_is_set(get_meta_subvol(), SUBVOL_IS_PROXY))
		/* 3, 4 */
		return vol_nr_origins(vol) - 1;
	/* 5, 6 */
	return meta_brick_belongs_dsa() ?
		vol_nr_origins(vol) - 1 : vol_nr_origins(vol) - 2;
}

static inline reiser4_subvol *subvol_by_key(const reiser4_key *key)
{
	return current_origin(get_key_ordering(key));
}

static inline int reserve_migration_iter(void)
{
	grab_space_enable();
	return reiser4_grab_reserved(reiser4_get_current_sb(),
				     estimate_migration_iter(),
				     BA_CAN_COMMIT,
				     get_meta_subvol());
}

static inline reiser4_subvol *get_proxy_subvol(void)
{
	assert("edward-2441", current_volume()->proxy != NULL);

	return current_volume()->proxy;
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
