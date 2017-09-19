/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

#if !defined(__REISER4_IOCTL_H__)
#define __REISER4_IOCTL_H__

#include <linux/fs.h>

/*
 * ioctl(2) command used to "unpack" reiser4 file, that is, convert it into
 * extents and fix in this state. This is used by applications that rely on
 *
 *     . files being block aligned, and
 *
 *     . files never migrating on disk
 *
 * for example, boot loaders (LILO) need this.
 *
 * This ioctl should be used as
 *
 *     result = ioctl(fd, REISER4_IOC_UNPACK);
 *
 * File behind fd descriptor will be converted to the extents (if necessary),
 * and its stat-data will be updated so that it will never be converted back
 * into tails again.
 */

#define REISER4_PATH_NAME_MAX 3900 /* FIXME: make it more precise */

typedef enum {
	REISER4_REGISTER_BRICK,
	REISER4_PRINT_VOLUME,
	REISER4_PRINT_BRICK,
	REISER4_EXPAND_BRICK,
	REISER4_SHRINK_BRICK,
	REISER4_ADD_BRICK,
	REISER4_REMOVE_BRICK,
	REISER4_BALANCE_VOLUME,
	REISER4_FORCED_BALANCE_VOLUME
} reiser4_vol_op;

struct reiser4_volume_stat
{
	u8  id[16]; /* unique ID */
	u64 nr_bricks; /* number of bricks in the array */
	u16 vpid; /* volume plugin ID */
	u16 dpid; /* distribution plugin ID */
	u64 state; /* unbalanced, etc flags */
};

struct reiser4_brick_stat
{
	u64 int_id; /* ordered number, 0 means meta-data brick */
	u8  ext_id[16]; /* external unique ID */
	u16 nr_replicas; /* number of replicas */
	u64 state; /* activated, etc flags */
	u64 block_count; /* total number of blocks */
	u64 data_room; /* number of data blocks */
	u64 blocks_used; /* number of blocks used by data and meta-data */
};

struct reiser4_vol_op_args
{
	reiser4_vol_op opcode;
	u64 delta;
	u64 brick_id;
	char name[REISER4_PATH_NAME_MAX + 1];
	union {
		struct reiser4_volume_stat vol;
		struct reiser4_brick_stat brick;
	}u;
};

#define REISER4_IOC_UNPACK _IOW(0xCD, 1, long)
#define REISER4_IOC_VOLUME _IOWR(0xCD, 2, struct reiser4_vol_op_args)

/* __REISER4_IOCTL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
