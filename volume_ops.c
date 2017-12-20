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
		vol->num_origins : - vol->num_origins;
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

	if (id >= vol->num_origins) {
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

static int reiser4_expand_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;

	if (!reiser4_trylock_volume(sb))
		return -EINVAL;

	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1949",
			"Can't expand brick of unbalanced volume");
		reiser4_unlock_volume(sb);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->expand_brick(super_volume(sb),
						       args->s.brick_id,
						       args->delta);
	reiser4_unlock_volume(sb);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb, 0);
}

static int reiser4_shrink_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;

	if (!reiser4_trylock_volume(sb))
		return -EINVAL;

	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1950",
			"Can't shrink brick of unbalanced volume");
		reiser4_unlock_volume(sb);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->shrink_brick(super_volume(sb),
						       args->s.brick_id,
						       args->delta);
	reiser4_unlock_volume(sb);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb, 0);
}

static int reiser4_add_brick(struct super_block *sb,
			     struct reiser4_vol_op_args *args)
{
	int ret;
	time_t start;
	reiser4_subvol *new = NULL;
	reiser4_volume *host_of_new = NULL;
	reiser4_context *ctx;

	ctx = reiser4_init_context(sb);
	if (IS_ERR(ctx)) {
		warning("edward-1975", "failed to init context");
		return PTR_ERR(ctx);
	}
	/*
	 * register new brick
	 */
	ret = reiser4_scan_device(args->d.name, FMODE_READ,
				  get_reiser4_fs_type(), &new, &host_of_new);
	if (ret)
		goto out;

	assert("edward-1969", new != NULL);
	assert("edward-1970", host_of_new != NULL);

	if (host_of_new != super_volume(sb)) {
		warning("edward-1971", "Can't add brick of other volume");
		ret = -EINVAL;
		goto out;
	}
	ret = reiser4_activate_subvol(sb, new);
	if (ret)
		goto out;
	/*
	 * add activated brick
	 */
	if (!reiser4_trylock_volume(sb)) {
		ret = -EINVAL;
		goto deactivate;
	}
	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1951", "Can't add brick to unbalanced volume");
		reiser4_unlock_volume(sb);
		ret = -EINVAL;
		goto deactivate;
	}
	ret = super_volume(sb)->vol_plug->add_brick(super_volume(sb),
						    new);
	reiser4_unlock_volume(sb);
	if (ret)
		goto deactivate;

	printk("reiser4 (%s): Brick %s has been added. Started balancing...\n",
	       sb->s_id, new->name);

	start = get_seconds();

	ret = super_volume(sb)->vol_plug->balance_volume(sb, 0);
	if (ret)
		goto deactivate;

	reiser4_exit_context(ctx);

	printk("reiser4 (%s): Balancing completed in %lu seconds.\n",
	       sb->s_id, get_seconds() - start);

	return 0;
 deactivate:
	reiser4_deactivate_subvol(sb, new);
 out:
	reiser4_exit_context(ctx);
	return ret;
}

static int reiser4_remove_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;

	if (!reiser4_trylock_volume(sb))
		return -EINVAL;

	if (reiser4_volume_test_set_unbalanced(sb)) {
		warning("edward-1952",
			"Can't remove brick from unbalanced volume");
		reiser4_unlock_volume(sb);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->remove_brick(super_volume(sb),
						       args->s.brick_id);
	reiser4_unlock_volume(sb);
	if (ret)
		return ret;
	return super_volume(sb)->vol_plug->balance_volume(sb, 0);
}

static int reiser4_balance_volume(struct super_block *sb)
{
	return super_volume(sb)->vol_plug->balance_volume(sb, 0);
}

/**
 * Perform balancing for volume marked as balanced.
 * This is for on-line file system check.
 */
static int reiser4_check_volume(struct super_block *sb)
{
	return super_volume(sb)->vol_plug->balance_volume(sb, 1);
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
	case REISER4_CHECK_VOLUME:
		return reiser4_check_volume(sb);
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
