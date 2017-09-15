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

static int reiser4_expand_volume(struct super_block *sb,
				 struct reiser4_vol_op_args *args)
{
	return -EINVAL;
}

static int reiser4_shrink_volume(struct super_block *sb,
				 struct reiser4_vol_op_args *args)
{
	return -EINVAL;
}

static int reiser4_balance_volume(struct super_block *sb)
{
	return super_volume(sb)->vol_plug->balance(sb);
}

int reiser4_volume_op(struct super_block *sb, struct reiser4_vol_op_args *args)
{
	switch(args->opcode) {
	case REISER4_SHRINK_VOLUME:
		return reiser4_shrink_volume(sb, args);
	case REISER4_EXPAND_VOLUME:
		return reiser4_expand_volume(sb, args);
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
