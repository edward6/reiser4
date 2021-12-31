/*
  Copyright (c) 2017-2020 Eduard O. Shishkin

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "debug.h"
#include "super.h"
#include "inode.h"
#include "plugin/volume/volume.h"

static int reiser4_register_brick(struct reiser4_vol_op_args *args)
{
	reiser4_volume *host = NULL;

	return reiser4_scan_device(args->d.name, FMODE_READ,
				   get_reiser4_fs_type(), NULL, &host,
				   &args->error);
}

static int reiser4_print_volume(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	return super_vol_plug(sb)->print_volume(sb, args);
}

static int reiser4_print_brick(struct super_block *sb,
			       struct reiser4_vol_op_args *args)
{
	return super_vol_plug(sb)->print_brick(sb, args);
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

static int reiser4_resize_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_subvol *this;
	int need_balance;

	if (reiser4_volume_has_incomplete_removal(sb)) {
		args->error = E_INCOMPL_REMOVAL;
		return -EBUSY;
	}
	if (args->new_capacity == 0) {
		args->error = E_RESIZE_TO_ZERO;
		return -EINVAL;
	}
	this = find_active_brick(sb, args->d.name);
	if (!this) {
		args->error = E_BRICK_NOT_IN_VOL;
		return -EINVAL;
	}
	if (args->new_capacity == this->data_capacity)
		/* nothing to do */
		return 0;
	ret = super_vol_plug(sb)->resize_brick(super_volume(sb),
				this,
				args->new_capacity - this->data_capacity,
				&need_balance, args);
	if (ret)
		/* resize operation should be repeated in regular context */
		return ret;

	if (!(args->flags & COMPLETE_WITH_BALANCE))
		return 0;

	if (!need_balance)
		return 0;

	ret = super_vol_plug(sb)->balance_volume(sb, 0);
	if (ret) {
		args->error = E_BALANCE;
		return ret;
	}
	/*
	 * clear unbalanced status on disk
	 */
	reiser4_volume_clear_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	return force_commit_current_atom();
}

static int reiser4_add_brick(struct super_block *sb,
			     struct reiser4_vol_op_args *args, int add_proxy)
{
	int ret;
	reiser4_volume *vol = super_volume(sb);
	int activated_here = 0;
	reiser4_subvol *new = NULL;
	reiser4_volume *host_of_new = NULL;

	if (reiser4_volume_has_incomplete_removal(sb)) {
		args->error = E_INCOMPL_REMOVAL;
		return -EBUSY;
	}
	/*
	 * register new brick
	 */
	ret = reiser4_scan_device(args->d.name, FMODE_READ,
				  get_reiser4_fs_type(), &new, &host_of_new,
				  &args->error);
	if (ret)
		return ret;

	assert("edward-1969", new != NULL);
	assert("edward-1970", host_of_new != NULL);

	if (host_of_new != vol) {
		args->error = E_ADD_INAPP_VOL;
		return -EINVAL;
	}
	if (!subvol_is_set(new, SUBVOL_ACTIVATED)) {
		new->flags |= (1 << SUBVOL_IS_ORPHAN);

		ret = reiser4_activate_subvol(sb, new);
		if (ret)
			return ret;
		activated_here = 1;
	}
	if (add_proxy) {
		if (brick_belongs_volume(vol, new) && is_proxy_brick(new)) {
			args->error = E_ADD_SECOND_PROXY;
			return -EINVAL;
		}
		assert("edward-2449",
		       ergo(!is_meta_brick(new),
			    subvol_is_set(new, SUBVOL_HAS_DATA_ROOM)));

		new->flags |= (1 << SUBVOL_IS_PROXY);
	}
	ret = vol->vol_plug->add_brick(vol, new, &args->error);
	if (ret) {
		/*
		 * operation of adding a brick should be repeated
		 * in regular context
		 */
		if (activated_here) {
			reiser4_deactivate_subvol(sb, new);
			reiser4_unregister_subvol(new);
		}
		return ret;
	}
	/*
	 * new volume configuration has been written to disk,
	 * so release all volinfo jnodes - they are not needed
	 * any more
	 */
	release_volinfo_nodes(&vol->volinfo[CUR_VOL_CONF], 0);
	clear_bit(SUBVOL_IS_ORPHAN, &new->flags);

	if (!(args->flags & COMPLETE_WITH_BALANCE))
		return 0;

	ret = vol->vol_plug->balance_volume(sb, 0);
	if (ret) {
		args->error = E_BALANCE;
		return ret;
	}
	/* clear unbalanced status on disk */

	reiser4_volume_clear_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	return force_commit_current_atom();
}

static void reiser4_detach_brick(reiser4_subvol *victim)
{
	struct ctx_brick_info *cbi;
	reiser4_context *ctx = get_current_context();
	struct rb_root *root = &ctx->bricks_info;
	reiser4_super_info_data *sbinfo = get_current_super_private();

	cbi = find_context_brick_info(ctx, victim->id);

	assert("edward-2257", cbi != NULL);

	__grabbed2free(cbi, sbinfo, cbi->grabbed_blocks, victim);

	rb_erase(&cbi->node, root);
	RB_CLEAR_NODE(&cbi->node);
	free_context_brick_info(cbi);

	victim->id = INVALID_SUBVOL_ID;
	victim->flags |= (1 << SUBVOL_IS_ORPHAN);
	reiser4_deactivate_subvol(victim->super, victim);
	reiser4_unregister_subvol(victim);
}

static int reiser4_finish_removal(struct super_block *sb, reiser4_volume *vol);

static int reiser4_remove_brick(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_volume *vol = super_volume(sb);
	reiser4_subvol *victim;

	if (reiser4_volume_has_incomplete_removal(sb)) {
		args->error = E_INCOMPL_REMOVAL;
		return -EBUSY;
	}
	victim = find_active_brick(sb, args->d.name);
	if (!victim) {
		args->error = E_BRICK_NOT_IN_VOL;
		return -EINVAL;
	}
	ret = vol->vol_plug->remove_brick(vol, victim, &args->error);
	if (ret)
		return ret;
	printk("reiser4 (%s): Brick %s scheduled for removal.\n",
	       sb->s_id, victim->name);

	release_volinfo_nodes(&vol->volinfo[CUR_VOL_CONF], 0);

	return reiser4_finish_removal(sb, vol);
}

static int reiser4_scale_volume(struct super_block *sb,
				struct reiser4_vol_op_args *args)
{
	int ret;
	reiser4_volume *vol = super_volume(sb);

	if (reiser4_volume_has_incomplete_removal(sb)) {
		args->error = E_INCOMPL_REMOVAL;
		return -EBUSY;
	}
	if (args->s.val == 0)
		return 0;
	ret = super_volume(sb)->vol_plug->scale_volume(sb, args->s.val);
	if (ret)
		return ret;
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	/*
	 * write unbalanced status to disk
	 */
	ret = force_commit_current_atom();
	if (ret)
		return ret;
	/*
	 * new volume configuration has been written to disk,
	 * so release all volinfo jnodes - they are not needed
	 * any more
	 */
	release_volinfo_nodes(&vol->volinfo[CUR_VOL_CONF], 0);

	printk("reiser4 (%s): Volume has beed scaled in %u times.",
	       sb->s_id, 1 << args->s.val);

	if (!(args->flags & COMPLETE_WITH_BALANCE))
		return 0;

	ret = vol->vol_plug->balance_volume(sb, 0);
	if (ret) {
		args->error = E_BALANCE;
		return ret;
	}
	/* clear unbalanced status on disk */

	reiser4_volume_clear_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	return force_commit_current_atom();
}

/**
 * We allow more than one balancing threads on the same volume. Note, however,
 * that it would be inefficient: others will be always going after one leader
 * without doing useful work.
 * Pre-condition: volume is read locked
 */
static int reiser4_balance_volume(struct super_block *sb, u32 flags)
{
	reiser4_volume *vol = super_volume(sb);
	int ret;

	ret = vol->vol_plug->balance_volume(sb, flags);
	if (ret)
		return ret;
	reiser4_volume_clear_unbalanced(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		return ret;
	return force_commit_current_atom();
}

/**
 * Pre-condition: exclusive access to the volume should be held
 */
static int reiser4_finish_removal(struct super_block *sb, reiser4_volume *vol)
{
	int ret;
	reiser4_subvol *victim;

	if (!reiser4_volume_has_incomplete_removal(sb))
		return 0;

	victim = vol->victim;
	if (!victim)
		goto cleanup;
	if (reiser4_volume_is_unbalanced(sb)) {
		/*
		 * Move out all data blocks from @victim to the
		 * remaining bricks. After balancing completion
		 * the @victim shoudn't contain busy data blocks,
		 * so we have to ignore immobile ststus of files
		 */
		ret = vol->vol_plug->balance_volume(sb,
					VBF_MIGRATE_ALL | VBF_CLR_IMMOBILE);
		if (ret)
			goto error;
		reiser4_volume_clear_unbalanced(sb);
	}
	/*
	 * at this point volume must have two distribution configs:
	 * old and new ones
	 */
	assert("edward-2258", vol->new_conf != NULL);

	ret = vol->vol_plug->remove_brick_tail(vol, victim);
	if (ret)
		goto error;
	assert("edward-2471", vol->new_conf == NULL);
 cleanup:
	assert("edward-2259", vol->victim == NULL);

	if (victim && !is_meta_brick(victim))
		/* Goodbye! */
		reiser4_detach_brick(victim);

	reiser4_volume_clear_incomplete_removal(sb);
	ret = capture_brick_super(get_meta_subvol());
	if (ret)
		goto error;
	ret = force_commit_current_atom();
	if (ret)
		goto error;
	printk("reiser4 (%s): Removal completed.\n", sb->s_id);
	return 0;
 error:
	reiser4_volume_set_incomplete_removal(sb);
	warning("", "Failed to complete brick removal on %s.", sb->s_id);
	return ret;
}

static int inode_set_immobile(struct inode *inode)
{
	if (reiser4_inode_get_flag(inode, REISER4_FILE_IMMOBILE))
		return 0;
	if (reserve_update_sd_common(inode))
		return RETERR(-ENOSPC);

	reiser4_inode_set_flag(inode, REISER4_FILE_IMMOBILE);
	return reiser4_update_sd(inode);
}

int inode_clr_immobile(struct inode *inode)
{
	if (!reiser4_inode_get_flag(inode, REISER4_FILE_IMMOBILE))
		return 0;
	if (reserve_update_sd_common(inode))
		return RETERR(-ENOSPC);

	reiser4_inode_clr_flag(inode, REISER4_FILE_IMMOBILE);
	return reiser4_update_sd(inode);
}

/**
 * Pre-condition: brick_removal_sem should be down for read
 */
static int reiser4_migrate_file(struct file *file, u64 dst_idx)
{
	int ret;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;

	/*
	 * We allow file migration on volumes with incompletely removed brick
	 */
	ret = super_vol_plug(sb)->migrate_file(inode, dst_idx);

	if (ret == 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		reiser4_txn_restart_current();
		grab_space_enable();
		ret = reiser4_sync_file_common(file, 0, LONG_MAX,
					       0 /* data and stat data */);
		if (ret)
			warning("edward-2463", "failed to sync file %llu",
				(unsigned long long)get_inode_oid(inode));
	}
	return ret;
}

/**
 * Reiser4 off-line volume operations (no FS are mounted).
 * This doesn't spawn transactions and executes not in reiser4 context.
 */
int reiser4_offline_op(struct reiser4_vol_op_args *args)
{
	int ret;

	switch(args->opcode) {
	case REISER4_REGISTER_BRICK:
		ret = reiser4_register_brick(args);
		break;
	case REISER4_UNREGISTER_BRICK:
		ret = reiser4_unregister_brick(args);
		break;
	case REISER4_VOLUME_HEADER:
		ret = reiser4_volume_header(args);
		break;
	case REISER4_BRICK_HEADER:
		ret = reiser4_brick_header(args);
		break;
	default:
		args->error = E_UNSUPP_OP;
		ret = -ENOTTY;
		break;
	}
	return ret;
}

/**
 * Reiser4 on-line volume operations (on mounted volumes).
 * Spawn transactions and performed in reiser4 context.
 */
int reiser4_volume_op_dir(struct file *file, struct reiser4_vol_op_args *args)
{
	int ret;
	struct super_block *sb = file_inode(file)->i_sb;
	reiser4_volume *vol = super_volume(sb);

	switch(args->opcode) {
	case REISER4_PRINT_VOLUME:
		if (!down_read_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_print_volume(sb, args);
		up_read(&vol->volume_sem);
		break;
	case REISER4_PRINT_BRICK:
		if (!down_read_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_print_brick(sb, args);
		up_read(&vol->volume_sem);
		break;
	case REISER4_RESIZE_BRICK:
		if (!down_write_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_resize_brick(sb, args);
		up_write(&vol->volume_sem);
		break;
	case REISER4_ADD_BRICK:
		if (!down_write_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_add_brick(sb, args, 0);
		up_write(&vol->volume_sem);
		break;
	case REISER4_ADD_PROXY:
		if (!down_write_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_add_brick(sb, args, 1);
		up_write(&vol->volume_sem);
		break;
	case REISER4_REMOVE_BRICK:
		if (!down_write_trylock(&vol->volume_sem))
			goto busy;
		if (!down_write_trylock(&vol->brick_removal_sem)) {
			up_write(&vol->volume_sem);
			goto busy;
		}
		ret = reiser4_remove_brick(sb, args);
		up_write(&vol->brick_removal_sem);
		up_write(&vol->volume_sem);
		break;
	case REISER4_FINISH_REMOVAL:
		down_write(&vol->volume_sem);
		down_write(&vol->brick_removal_sem);
		ret = reiser4_finish_removal(sb, vol);
		up_write(&vol->brick_removal_sem);
		up_write(&vol->volume_sem);
		break;
	case REISER4_SCALE_VOLUME:
		if (!down_write_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_scale_volume(sb, args);
		up_write(&vol->volume_sem);
		break;
	case REISER4_BALANCE_VOLUME:
		if (!down_read_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_balance_volume(sb, 0);
		up_read(&vol->volume_sem);
		break;
	case REISER4_RESTORE_REGULAR_DST:
		if (!down_read_trylock(&vol->volume_sem))
			goto busy;
		ret = reiser4_balance_volume(sb,
					     VBF_MIGRATE_ALL | VBF_CLR_IMMOBILE);
		up_read(&vol->volume_sem);
		break;
	default:
		args->error = E_UNSUPP_OP;
		ret = RETERR(-ENOTTY);
		break;
	}
	return ret;
 busy:
	args->error = E_VOLUME_BUSY;
	return RETERR(-EBUSY);
}

int reiser4_volume_op_file(struct file *file,  struct reiser4_vol_op_args *args)
{
	int ret;
	struct super_block *sb = file_inode(file)->i_sb;
	reiser4_volume *vol = super_volume(sb);

	switch(args->opcode) {
	case REISER4_MIGRATE_FILE:
		/*
		 * make sure that bricks won't be evicted during file migration
		 */
		down_read(&vol->brick_removal_sem);
		ret = reiser4_migrate_file(file, args->s.brick_idx);
		up_read(&vol->brick_removal_sem);
		break;
	case REISER4_SET_FILE_IMMOBILE:
		ret = inode_set_immobile(file_inode(file));
		break;
	case REISER4_CLR_FILE_IMMOBILE:
		ret = inode_clr_immobile(file_inode(file));
		break;
	default:
		args->error = E_UNSUPP_OP;
		ret = RETERR(-ENOTTY);
		break;
	}
	return ret;
}

long reiser4_ioctl_volume(struct file *file,
			  unsigned int cmd, unsigned long arg,
			  int (*volume_op)(struct file *file,
					   struct reiser4_vol_op_args *args))
{
	int ret;
	reiser4_context *ctx;

	ctx = reiser4_init_context(file_inode(file)->i_sb);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	switch (cmd) {
	case REISER4_IOC_VOLUME: {
		struct reiser4_vol_op_args *op_args;

		if (!capable(CAP_SYS_ADMIN))
			return RETERR(-EPERM);

		op_args = memdup_user((void __user *)arg, sizeof(*op_args));
		if (IS_ERR(op_args))
			return PTR_ERR(op_args);

		ret = volume_op(file, op_args);
		if (copy_to_user((struct reiser4_vol_op_args __user *)arg,
				 op_args, sizeof(*op_args)))
			ret = RETERR(-EFAULT);
		kfree(op_args);
		break;
	}
	default:
		ret = RETERR(-ENOTTY);
		break;
	}
	reiser4_exit_context(ctx);
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
