/*
  Copyright (c) 2017 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "debug.h"
#include "super.h"
#include "ioctl.h"

static int reiser4_register_brick(struct super_block *sb,
				  struct reiser4_vol_op_args *args)
{
	return reiser4_scan_device(args->name, FMODE_READ,
				   get_reiser4_fs_type(), NULL);
}

static int reiser4_print_volume(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	reiser4_volume *vol = super_volume(sb);

	args->u.vol.nr_bricks = vol->num_origins;
	memcpy(args->u.vol.id, vol->uuid, 16);
	args->u.vol.vpid = vol->vol_plug->h.id;
	args->u.vol.dpid = vol->dist_plug->h.id;
	return 0;
}

static int reiser4_print_brick(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	int ret = 0;
	u64 id = args->brick_id;
	reiser4_volume *vol = super_volume(sb);
	reiser4_subvol *subv;

	spin_lock_reiser4_super(get_super_private(sb));

	if (id >= vol->num_origins) {
		ret = -EINVAL;
		goto out;
	}
	subv = vol->subvols[id][0];
	strncpy(args->name, subv->name, REISER4_PATH_NAME_MAX + 1);
	memcpy(args->u.brick.ext_id, subv->uuid, 16);
	args->u.brick.int_id = subv->id;
	args->u.brick.nr_replicas = subv->num_replicas;
	args->u.brick.block_count = subv->block_count;
	args->u.brick.data_room = subv->data_room;
	args->u.brick.blocks_used = subv->blocks_used;
 out:
	spin_unlock_reiser4_super(get_super_private(sb));
	return ret;
}

static int reiser4_expand_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	ret = super_volume(sb)->vol_plug->expand_brick(super_volume(sb),
						       args->brick_id,
						       args->delta);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_shrink_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	ret = super_volume(sb)->vol_plug->shrink_brick(super_volume(sb),
						       args->brick_id,
						       args->delta);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_add_brick(struct super_block *sb,
			     struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *new;
	/*
	 * register new brick
	 */
	ret = reiser4_scan_device(args->name, FMODE_READ,
				  get_reiser4_fs_type(), &new);
	if (ret)
		return ret;
	/*
	 * add registered brick
	 */
	ret = super_volume(sb)->vol_plug->add_brick(super_volume(sb), new);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_remove_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	ret = super_volume(sb)->vol_plug->remove_brick(super_volume(sb),
						       args->brick_id);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb);
}

static int reiser4_balance_volume(struct super_block *sb)
{
	return super_volume(sb)->vol_plug->balance_volume(sb);
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
