#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o

EXTRA_CFLAGS += \
	       -Wpointer-arith \
	       -Wwrite-strings \
	       -Woverloaded-virtual \
           -Wformat \
	       -Wsynth \
	       -Wundef \
	       -Wcast-align \
	       -Wlarger-than-4096 \
	       -Wnon-virtual-dtor \
	       -Wreorder \
	       -Wsign-promo \
           -Wuninitialized \
           -Wunused \
	       -Wcomment \
           \
	       -Wno-nested-externs \
	       -Wno-sign-compare

#	       -Winline \

ifeq ($(CONFIG_REISER4_NOOPT),y)
	EXTRA_CFLAGS += -O0
endif

reiser4-objs := \
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
		   lnode.o \
		   kcond.o \
		   latch.o \
		   seal.o \
		   scint.o \
		   dscale.o \
		   trace.o \
		   flush_queue.o \
		   ktxnmgrd.o \
		   kattr.o \
		   blocknrset.o \
		   super.o \
		   tree_walk.o \
		   inode.o \
		   vfs_ops.o \
		   inode_ops.o \
		   file_ops.o \
		   as_ops.o \
		   emergency_flush.o \
		   spinprof.o\
		   entd.o\
		   readahead.o \
	           crypt.o \
		   compress.o\
		   diskmap.o \
           \
		   plugin/plugin.o \
		   plugin/plugin_set.o \
		   plugin/plugin_hash.o \
		   plugin/node/node.o \
		   plugin/node/node40.o \
		   plugin/object.o \
		   plugin/symlink.o \
		   plugin/cryptcompress.o \
           \
		   plugin/item/static_stat.o \
		   plugin/item/sde.o \
		   plugin/item/cde.o \
		   plugin/item/internal.o \
		   plugin/item/tail.o \
		   plugin/item/ctail.o \
           \
		   plugin/hash.o \
		   plugin/tail_policy.o \
		   plugin/item/item.o \
           \
		   plugin/dir/hashed_dir.o \
		   plugin/dir/dir.o \
           \
		   plugin/security/perm.o \
		   plugin/security/acl.o \
           \
		   plugin/pseudo/pseudo.o \
           \
           plugin/oid/oid40.o \
           plugin/oid/oid.o \
           \
           plugin/space/bitmap.o \
           plugin/space/test.o \
		   plugin/space/space_allocator.o \
           \
           plugin/disk_format/disk_format40.o \
           plugin/disk_format/test.o \
           plugin/disk_format/disk_format.o \
           \
		   plugin/item/extent.o \
		   plugin/file/file.o \
           plugin/file/tail_conversion.o

#$parser/tmp: $/parser/*.[chy]
#	$(MAKE) -C parser

