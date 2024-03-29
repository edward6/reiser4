#
# reiser4/Makefile
#

MODULE := reiser4

obj-$(CONFIG_REISER4_FS) := $(MODULE).o

$(MODULE)-objs += \
			debug.o \
			jnode.o \
			znode.o \
			key.o \
			pool.o \
			tree_mod.o \
			estimate.o \
			carry.o \
			carry_ops.o \
			lock.o \
			tree.o \
			context.o \
			tap.o \
			coord.o \
			block_alloc.o \
			txnmgr.o \
			kassign.o \
			flush.o \
			wander.o \
			eottl.o \
			search.o \
			page_cache.o \
			seal.o \
			dscale.o \
			flush_queue.o \
			ktxnmgrd.o \
			blocknrset.o \
			super.o \
			super_ops.o \
			fsdata.o \
			export_ops.o \
			oid.o \
			tree_walk.o \
			inode.o \
			vfs_ops.o \
			as_ops.o \
			entd.o \
			readahead.o \
			status_flags.o \
			init_super.o \
			safe_link.o \
			blocknrlist.o \
			discard.o \
			checksum.o \
		\
			plugin/plugin.o \
			plugin/plugin_set.o \
			plugin/object.o \
			plugin/cluster.o \
			plugin/txmod.o \
			plugin/inode_ops.o \
			plugin/inode_ops_rename.o \
			plugin/file_ops.o \
			plugin/file_ops_readdir.o \
			plugin/dir_plugin_common.o \
			plugin/file_plugin_common.o \
			plugin/hash.o \
			plugin/fibration.o \
			plugin/tail_policy.o \
		\
			plugin/file/file.o \
			plugin/file/tail_conversion.o \
			plugin/file/file_conversion.o \
			plugin/file/symlink.o \
			plugin/file/cryptcompress.o \
		\
			plugin/dir/hashed_dir.o \
			plugin/dir/seekable_dir.o \
		\
			plugin/node/node.o \
			plugin/node/node40.o \
			plugin/node/node41.o \
		\
			plugin/crypto/cipher.o \
			plugin/crypto/digest.o \
		\
			plugin/compress/compress.o \
			plugin/compress/compress_mode.o \
		\
			plugin/item/static_stat.o \
			plugin/item/sde.o \
			plugin/item/cde.o \
			plugin/item/blackbox.o \
			plugin/item/internal.o \
			plugin/item/tail.o \
			plugin/item/ctail.o \
			plugin/item/extent.o \
			plugin/item/extent_item_ops.o \
			plugin/item/extent_file_ops.o \
			plugin/item/extent_flush_ops.o \
			plugin/item/item.o \
		\
			plugin/security/perm.o \
		\
			plugin/space/bitmap.o \
		\
			plugin/disk_format/disk_format40.o \
			plugin/disk_format/disk_format.o

CFLAGS_REMOVE_carry_ops.o				= -Wimplicit-fallthrough
CFLAGS_REMOVE_tree.o					= -Wimplicit-fallthrough
CFLAGS_REMOVE_search.o					= -Wimplicit-fallthrough
CFLAGS_REMOVE_plugin/file/cryptcompress.o		= -Wimplicit-fallthrough
CFLAGS_REMOVE_plugin/node/node40.o			= -Wimplicit-fallthrough
CFLAGS_REMOVE_plugin/compress/compress.o		= -Wimplicit-fallthrough
CFLAGS_REMOVE_plugin/item/internal.o			= -Wimplicit-fallthrough
CFLAGS_REMOVE_plugin/disk_format/disk_format40.o	= -Wimplicit-fallthrough
