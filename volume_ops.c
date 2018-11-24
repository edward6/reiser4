/*
  Copyright (c) 2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "debug.h"
#include "super.h"
#include "plugin/volume/volume.h"

static int reiser4_register_brick(struct super_block *sb,
				  struct reiser4_vol_op_args *args)
{
	reiser4_volume *host = NULL;

	return reiser4_scan_device(args->d.name, FMODE_READ,
				   get_reiser4_fs_type(), NULL, &host);
}

static int reiser4_print_volume(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	reiser4_volume *vol = super_volume(sb);

	args->u.vol.nr_bricks = meta_brick_belongs_aid() ?
		vol_nr_origins(vol) : - vol_nr_origins(vol);
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	args->u.vol.fs_flags = get_super_private(sb)->fs_flags;
	args->u.vol.nr_volinfo_blocks = vol->num_volmaps + vol->num_voltabs;
	return 0;
}

static int reiser4_print_brick(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	int ret = 0;
	u64 id = args->s.brick_id;
	reiser4_volume *vol = super_volume(sb);
	reiser4_subvol *subv;

	spin_lock_reiser4_super(get_super_private(sb));

	if (id >= vol_nr_origins(vol)) {
		ret = -EINVAL;
		goto out;
	}
	subv = vol->subvols[id][0];
	strncpy(args->d.name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_room = subv->data_room;
	args->u.brick.blocks_used = subv->blocks_used;
	args->u.brick.volinfo_addr = subv->volmap_loc;
 out:
	spin_unlock_reiser4_super(get_super_private(sb));
	return ret;
}

/**
 * Accept ordered number of a voltab block and dump its content
 * to a memory buffer.
 */
static int reiser4_print_voltab(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	u64 idx = args->s.voltab_nr;
	distribution_plugin *dist_plug = super_volume(sb)->dist_plug;

	dist_plug->v.dump(&super_volume(sb)->aid,
			  args->d.data + (idx << sb->s_blocksize_bits),
			  idx << sb->s_blocksize_bits,
			  sb->s_blocksize);
	return 0;
}

/**
 * find activated brick by @name
 */
static reiser4_subvol *find_active_brick(char *name)
{
	u32 subv_id;
	reiser4_subvol *result = NULL;

	for_each_origin(subv_id) {
		if (!strcmp(current_origin(subv_id)->name, name)) {
			result = current_origin(subv_id);
			break;
		}
	}
	return result;
}

static int reiser4_expand_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;

	reiser4_subvol *victim;

	victim = find_active_brick(args->d.name);
	if (!victim) {
		warning("edward-2147",
			"Brick %s doesn't belong to volume %s. Can not expand.",
			args->d.name,
			reiser4_get_current_sb()->s_id);
		return -EINVAL;
	}
	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1949",
			"Can't expand brick of unbalanced volume");
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->expand_brick(super_volume(sb),
						       victim,
						       args->delta);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_shrink_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *victim;

	victim = find_active_brick(args->d.name);
	if (!victim) {
		warning("edward-2148",
			"Brick %s doesn't belong to volume %s. Can not shrink.",
			args->d.name,
			reiser4_get_current_sb()->s_id);
		return -EINVAL;
	}
	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1950",
			"Can't shrink brick of unbalanced volume");
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->shrink_brick(super_volume(sb),
						       victim, args->delta);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_add_brick(struct super_block *sb,
			     struct reiser4_vol_op_args *args)
{
	int ret;
	time_t start;
	reiser4_subvol *new = NULL;
	reiser4_volume *host_of_new = NULL;

	/*
	 * Get exclusive access to the volume.
	 * FIXME: Add and use a special SUBVOL_BUSY flag for this purpose
	 */
	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1951", "Can't add brick to unbalanced volume");
		return -EINVAL;
	}
	/*
	 * register new brick
	 */
	ret = reiser4_scan_device(args->d.name, FMODE_READ,
				  get_reiser4_fs_type(), &new, &host_of_new);
	if (ret)
		return ret;

	assert("edward-1969", new != NULL);
	assert("edward-1970", host_of_new != NULL);

	if (host_of_new != super_volume(sb)) {
		warning("edward-1971",
			"Failed to add brick (Inappropriate volume)");
		return -EINVAL;
	}
	new->flags |= (1 << SUBVOL_IS_ORPHAN);

	ret = reiser4_activate_subvol(sb, new);
	if (ret)
		return ret;
	/*
	 * add activated new brick
	 */
	ret = super_volume(sb)->vol_plug->add_brick(super_volume(sb),
						    new);
	if (ret)
		goto deactivate;

	printk("reiser4 (%s): Brick %s has been added. Started balancing...\n",
	       sb->s_id, new->name);

	start = get_seconds();

	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret) {
		/*
		 * it is not possible to deactivate the new
		 * brick already: there can be IO requests
		 * issued at the beginning of re-balancing
		 */
		warning("edward-2139",
			"%s: Balancing aborted (%d)", sb->s_id, ret);
		return ret;
	}
	printk("reiser4 (%s): Balancing completed in %lu seconds.\n",
	       sb->s_id, get_seconds() - start);

	clear_bit(SUBVOL_IS_ORPHAN, &new->flags);
	reiser4_volume_clear_unbalanced(sb);
	/*
	 * clear unbalanced status in format super-block
	 */
	return capture_brick_super(get_meta_subvol());
 deactivate:
	reiser4_deactivate_subvol(sb, new);
	return ret;
}

static int reiser4_remove_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *victim;

	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1952",
			"Can't remove brick from unbalanced volume");
		return -EINVAL;
	}
	victim = find_active_brick(args->d.name);
	if (!victim) {
		warning("edward-2149",
			"Brick %s doesn't belong to volume %s. Can not remove.",
			args->d.name,
			reiser4_get_current_sb()->s_id);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->remove_brick(super_volume(sb),
						       victim);
	if (ret)
		return ret;

	if (!is_meta_brick(victim)) {
		victim->id = INVALID_SUBVOL_ID;
		victim->flags |= (1 << SUBVOL_IS_ORPHAN);
		reiser4_deactivate_subvol(sb, victim);
		reiser4_unregister_subvol(victim);
		/*
		 * now the block device can be safely removed
		 * from the machine
		 */
	}
	reiser4_volume_clear_unbalanced(sb);
	/*
	 * clear unbalanced status in format super-block
	 */
	return capture_brick_super(get_meta_subvol());
}

static int reiser4_balance_volume(struct super_block *sb)
{
	int ret;
	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1952",
			"Balancing of volume %s is in proggress", sb->s_id);
		return -EINVAL;
	}
	/*
	 * set unbalanced status to format super-block
	 */
	ret = capture_brick_super(get_meta_subvol());
	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret)
		return ret;
	reiser4_volume_clear_unbalanced(sb);
	/*
	 * clear unbalanced status in format super-block
	 */
	return capture_brick_super(get_meta_subvol());
}

int reiser4_volume_op(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	switch(args->opcode) {
	case REISER4_REGISTER_BRICK:
		return reiser4_register_brick(sb, args);
	case REISER4_PRINT_VOLUME:
		return reiser4_print_volume(sb, args);
	case REISER4_PRINT_BRICK:
		return reiser4_print_brick(sb, args);
	case REISER4_PRINT_VOLTAB:
		return reiser4_print_voltab(sb, args);
	case REISER4_EXPAND_BRICK:
		return reiser4_expand_brick(sb, args);
	case REISER4_SHRINK_BRICK:
		return reiser4_shrink_brick(sb, args);
	case REISER4_ADD_BRICK:
		return reiser4_add_brick(sb, args);
	case REISER4_REMOVE_BRICK:
		return reiser4_remove_brick(sb, args);
	case REISER4_BALANCE_VOLUME:
		return reiser4_balance_volume(sb);
	default:
		return -EINVAL;
	}
}

/*
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 80
 * scroll-step: 1
 * End:
 */
