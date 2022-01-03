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
 * On-line per-volume and per-subvolume flags.
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
	/* volume is in unbalanced state */
	REISER4_UNBALANCED_VOL = 12,
	/* this flag indicates that volume operation was
	   interrupted for some reasons (e.g. system crash),
	   and should be completed in some context */
	REISER4_INCOMPLETE_BRICK_REMOVAL = 13,
	/* proxy-subvolume is active */
	REISER4_PROXY_ENABLED = 15,
	/* proxy subvolume accepts IO requests */
	REISER4_PROXY_IO = 16
} reiser4_fs_flag;

typedef enum {
	/* set if all nodes in internal tree have the same
	 * node layout plugin. See znode_guess_plugin() */
	SUBVOL_ONE_NODE_PLUGIN = 0,
	/* set if subvolume lives on a solid state drive */
	SUBVOL_IS_NONROT_DEVICE = 1,
	/* set if subvol is registered */
	SUBVOL_REGISTERED = 2,
	/* set if subvol is activated */
	SUBVOL_ACTIVATED = 3,
	/* set if brick is used for data storage and participates
	   in regular data distribution */
	SUBVOL_HAS_DATA_ROOM = 4,
	/* set if subvolume is not included in volume configuration
	   and doesn't accept any IOs */
	SUBVOL_IS_ORPHAN = 5,
	/* set if brick was scheduled for removal. It may be not
	   empty and may accept IOs */
	SUBVOL_TO_BE_REMOVED = 6,
	/* set if brick is used for data storage, but doesn't
	   participate in regular data distribution */
	SUBVOL_IS_PROXY = 7
} reiser4_subvol_flag;

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
	REISER4_RESIZE_BRICK,
	REISER4_ADD_BRICK,
	REISER4_REMOVE_BRICK,
	REISER4_SCALE_VOLUME,
	REISER4_BALANCE_VOLUME,
	REISER4_ADD_PROXY,
	REISER4_MIGRATE_FILE,
	REISER4_SET_FILE_IMMOBILE,
	REISER4_CLR_FILE_IMMOBILE,
	REISER4_FINISH_REMOVAL,
	REISER4_RESTORE_REGULAR_DST
} reiser4_vol_op;

typedef enum {
	E_NO_ERROR = 0,
	E_NO_BRICK = 1,
	E_NO_VOLUME = 2,
	E_RESIZE_PROXY = 3,
	E_RESIZE_SIMPLE = 4,
	E_RESIZE_TO_ZERO = 5,
	E_ADD_INVAL_CAPA = 6,
	E_ADD_NOT_EMPTY = 7,
	E_ADD_SIMPLE = 8,
	E_ADD_INAPP_VOL = 9,
	E_ADD_SECOND_PROXY = 10,
	E_ADD_SNGL_PROXY = 11,
	E_BRICK_EXIST = 12,
	E_BRICK_NOT_IN_VOL = 13,
	E_REMOVE_SIMPLE = 14,
	E_REMOVE_UNDEF = 15,
	E_REMOVE_NOSPACE = 16,
	E_REMOVE_MTD = 17,
	E_REMOVE_TAIL_SIMPLE = 18,
	E_REMOVE_TAIL_NOT_EMPTY = 19,
	E_BALANCE_SIMPLE = 20,
	E_BALANCE_MIGR_ERROR = 21,
	E_BALANCE_CLR_IMMOB = 22,
	E_INCOMPL_REMOVAL = 23,
	E_VOLUME_BUSY = 24,
	E_REG_NO_MASTER = 25,
	E_UNREG_ACTIVE = 26,
	E_UNREG_NO_BRICK = 27,
	E_SCAN_UNSUPP = 28,
	E_SCAN_UNMATCH = 29,
	E_SCAN_BAD_STRIPE = 30,
	E_UNSUPP_OP = 31
} reiser4_vol_op_error;

static inline void set_vol_op_error(reiser4_vol_op_error *errp,
				    reiser4_vol_op_error val)
{
	if (errp)
		*errp = val;
}

typedef enum {
	COMPLETE_WITH_BALANCE = 0x1
} reiser4_vol_op_flags;

struct reiser4_volume_stat
{
	u8  id[16]; /* unique ID */
	u32 nr_bricks; /* total number of bricks in the volume */
	u32 bricks_in_dsa; /* number of bricks in DSA */
	u16 vpid; /* volume plugin ID */
	u16 dpid; /* distribution plugin ID */
	u16 stripe_bits; /* logarithm of stripe size */
	u16 nr_sgs_bits; /* logarithm of number of hash space segments */
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
	u64 subv_flags; /* per-subvolume on-line flags */
	u64 block_count; /* total number of blocks on the device */
	u64 data_capacity; /* "weight" of the brick in data storage array */
	u64 blocks_used; /* number of blocks used by data and meta-data */
	u64 system_blocks; /* minimal number of blocks, which are occupied by
			      system data (super-blocks, bitmap blocks, etc) */
	u64 volinfo_addr; /* disk address of the first block of a portion
			     of volume configuration stored on this brick */
};

struct reiser4_vol_op_args
{
	reiser4_vol_op opcode;
	reiser4_vol_op_error error;
	u64 new_capacity;
	u64 flags;
	union {
		u64 brick_idx; /* index of brick in logical volume */
		u64 vol_idx; /* serial num of volume in the list of volumes */
		u64 val;
	}s;
	union {
		char name[REISER4_PATH_NAME_MAX + 1];
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
