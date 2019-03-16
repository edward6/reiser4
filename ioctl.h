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

/*
 * Per volume flags.
 * They are set up for (re)mount session and are not stored on disk
 */
typedef enum {
	/*
	 * True if this file system doesn't support hard-links (multiple names)
	 * for directories: this is default UNIX behavior.
	 *
	 * If hard-links on directoires are not allowed, file system is Acyclic
	 * Directed Graph (modulo dot, and dotdot, of course).
	 *
	 * This is used by reiser4_link().
	 */
	REISER4_ADG = 0,
	/* if set, bsd gid assignment is supported. */
	REISER4_BSD_GID = 2,
	/* [mac]_time are 32 bit in inode */
	REISER4_32_BIT_TIMES = 3,
	/* load all bitmap blocks at mount time */
	REISER4_DONT_LOAD_BITMAP = 5,
	/* enforce atomicity during write(2) */
	REISER4_ATOMIC_WRITE = 6,
	/* enable issuing of discard requests */
	REISER4_DISCARD = 8,
	/* disable hole punching at flush time */
	REISER4_DONT_PUNCH_HOLES = 9,
	/* volume is ready for regular operations */
	REISER4_ACTIVATED_VOL = 10,
	/* this is to serialize online volume operations */
	REISER4_BUSY_VOL = 11,
	/* volume is in unbalanced state */
	REISER4_UNBALANCED_VOL = 12,
	/* volume operation was not completed */
	REISER4_INCOMPLETE_BRICK_REMOVAL = 13
} reiser4_fs_flag;

#define REISER4_PATH_NAME_MAX 3900 /* FIXME: make it more precise */

typedef enum {
	REISER4_INVALID_OPT,
	REISER4_REGISTER_BRICK,
	REISER4_UNREGISTER_BRICK,
	REISER4_LIST_BRICKS,
	REISER4_VOLUME_HEADER,
	REISER4_BRICK_HEADER,
	REISER4_PRINT_VOLUME,
	REISER4_PRINT_BRICK,
	REISER4_PRINT_VOLTAB,
	REISER4_EXPAND_BRICK,
	REISER4_SHRINK_BRICK,
	REISER4_ADD_BRICK,
	REISER4_REMOVE_BRICK,
	REISER4_SCALE_VOLUME,
	REISER4_BALANCE_VOLUME,
	REISER4_CHECK_VOLUME
} reiser4_vol_op;

struct reiser4_volume_stat
{
	u8  id[16]; /* unique ID */
	s64 nr_bricks; /* absolute value indicates total number of
			  bricks in the volume. Negative means that
			  AID doesn't contain meta-data brick */
	u16 vpid; /* volume plugin ID */
	u16 dpid; /* distribution plugin ID */
	u16 stripe_bits; /* logarithm of stripe size */
	u64 fs_flags; /* the same as the one of private super-block */
	u32 nr_mslots; /* number of slots */
	u32 nr_volinfo_blocks; /* Total number of blocks in the set
				  where volume configuration is stored */
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
	u64 system_blocks; /* minimal number of blocks, which are occupied by
			      system data (super-blocks, bitmap blocks, etc) */
	u64 volinfo_addr; /* disk address of the first block of a portion
			     of volume configuration stored on this brick */
};

struct reiser4_vol_op_args
{
	reiser4_vol_op opcode;
	int error;
	u64 delta;
	union {
		u64 brick_idx; /* index of brick in logical volume */
		u64 vol_idx; /* serial num of volume in the list of volumes */
		u64 voltab_nr; /* index of voltab unformatted block */
	}s;
	union {
		char name[REISER4_PATH_NAME_MAX + 1];
		char data[4096];
	}d;
	struct {
		struct reiser4_volume_stat vol;
		struct reiser4_brick_stat brick;
	}u;
};

#define REISER4_IOC_UNPACK _IOW(0xCD, 1, long)
#define REISER4_IOC_VOLUME _IOWR(0xCD, 2, struct reiser4_vol_op_args)
#define REISER4_IOC_SCAN_DEV _IOWR(0xCD, 3, struct reiser4_vol_op_args)

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
