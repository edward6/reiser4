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
	return super_volume(sb)->vol_plug->print_volume(sb, args);
}

static int reiser4_print_brick(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	return super_volume(sb)->vol_plug->print_brick(sb, args);
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
			  sb->s_blocksize, CUR_VOL_CONF);
	return 0;
}

/**
 * find activated brick by @name
 */
static reiser4_subvol *find_active_brick(struct super_block *super,
					 char *name)
{
	u32 subv_id;
	reiser4_subvol *result = NULL;
	lv_conf *conf = super_conf(super);

	for_each_mslot(conf, subv_id) {
		if (!conf_mslot_at(conf, subv_id))
			continue;
		if (!strcmp(conf_origin(conf, subv_id)->name, name)) {
			result = conf_origin(conf, subv_id);
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

	if (reiser4_volume_is_unbalanced(sb)) {
		warning("edward-2165",
			"Failed to expand brick (Unbalanced volume %s)",
			sb->s_id);
		return -EINVAL;
	}
	victim = find_active_brick(sb, args->d.name);
	if (!victim) {
		warning("edward-2147",
			"Brick %s doesn't belong to volume %s. Can not expand.",
			args->d.name,
			reiser4_get_current_sb()->s_id);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->expand_brick(super_volume(sb),
						       victim,
						       args->delta);
	if (ret)
		return ret;
	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret)
		return ret;
	reiser4_volume_clear_unbalanced(sb);
	return capture_brick_super(get_meta_subvol());
}

static int reiser4_shrink_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *victim;

	if (reiser4_volume_is_unbalanced(sb)) {
		warning("edward-2166",
			"Failed to shrink brick (Unbalanced volume %s)",
			sb->s_id);
		return -EINVAL;
	}
	victim = find_active_brick(sb, args->d.name);
	if (!victim) {
		warning("edward-2148",
			"Brick %s doesn't belong to volume %s. Can not shrink.",
			args->d.name,
			reiser4_get_current_sb()->s_id);
		return -EINVAL;
	}
	ret = super_volume(sb)->vol_plug->shrink_brick(super_volume(sb),
						       victim, args->delta);
	if (ret)
		return ret;
	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret)
		return ret;
	reiser4_volume_clear_unbalanced(sb);
	return capture_brick_super(get_meta_subvol());
}

static int reiser4_add_brick(struct super_block *sb,
			     struct reiser4_vol_op_args *args)
{
	int ret;
	int activated_here = 0;
	reiser4_subvol *new = NULL;
	reiser4_volume *host_of_new = NULL;

	if (reiser4_volume_is_unbalanced(sb)) {
		warning("edward-2167",
			"Failed to add brick (Unbalanced volume %s)",
			sb->s_id);
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
	if (!subvol_is_set(new, SUBVOL_ACTIVATED)) {
		new->flags |= (1 << SUBVOL_IS_ORPHAN);

		ret = reiser4_activate_subvol(sb, new);
		if (ret)
			return ret;
		activated_here = 1;
	}
	/*
	 * add activated new brick
	 */
	ret = super_volume(sb)->vol_plug->add_brick(super_volume(sb),
						    new);
	if (ret) {
		if (activated_here) {
			reiser4_deactivate_subvol(sb, new);
			reiser4_unregister_subvol(new);
		}
		return ret;
	}
	clear_bit(SUBVOL_IS_ORPHAN, &new->flags);

	printk("reiser4 (%s): Brick %s has been added.", sb->s_id, new->name);

	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret)
		/*
		 * it is not possible to deactivate the new
		 * brick already: there can be IO requests
		 * issued at the beginning of re-balancing
		 */
		return ret;
	reiser4_txn_restart_current();

	reiser4_volume_clear_unbalanced(sb);
	return capture_brick_super(get_meta_subvol());
}

static int reiser4_remove_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *victim;

	if (reiser4_volume_is_unbalanced(sb)) {
		warning("edward-2168",
			"Failed to remove brick (Unbalanced volume %s)",
			sb->s_id);
		return -EINVAL;
	}
	victim = find_active_brick(sb, args->d.name);
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
	return capture_brick_super(get_meta_subvol());
}

static int reiser4_balance_volume(struct super_block *sb)
{
	int ret;

	if (!reiser4_volume_is_unbalanced(sb))
		return 0;
	/*
	 * volume must have two distribution configs:
	 * old and new ones
	 */
	ret = super_volume(sb)->vol_plug->balance_volume(sb);
	if (ret)
		return ret;
	reiser4_volume_clear_unbalanced(sb);
	return capture_brick_super(get_meta_subvol());
}

int reiser4_volume_op(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	int ret;
	/*
	 * take exclusive access
	 */
	if (reiser4_volume_test_set_busy(sb)) {
		warning("edward-1952",
			"Can't operate on busy volume %s", sb->s_id);
		return -EINVAL;
	}
	switch(args->opcode) {
	case REISER4_REGISTER_BRICK:
		ret = reiser4_register_brick(sb, args);
		break;
	case REISER4_PRINT_VOLUME:
		ret = reiser4_print_volume(sb, args);
		break;
	case REISER4_PRINT_BRICK:
		ret = reiser4_print_brick(sb, args);
		break;
	case REISER4_PRINT_VOLTAB:
		ret = reiser4_print_voltab(sb, args);
		break;
	case REISER4_EXPAND_BRICK:
		ret = reiser4_expand_brick(sb, args);
		break;
	case REISER4_SHRINK_BRICK:
		ret = reiser4_shrink_brick(sb, args);
		break;
	case REISER4_ADD_BRICK:
		ret = reiser4_add_brick(sb, args);
		break;
	case REISER4_REMOVE_BRICK:
		ret = reiser4_remove_brick(sb, args);
		break;
	case REISER4_BALANCE_VOLUME:
		ret = reiser4_balance_volume(sb);
		break;
	default:
		warning("edward-1950",
			"%s: unknown volume operation %d",
			sb->s_id, args->opcode);
		ret = -EINVAL;
		break;
	}
	/* drop exclusive access */
	reiser4_volume_clear_busy(sb);
	return ret;
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
