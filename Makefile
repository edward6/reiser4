#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o
ifdef CONFIG_REISER4_CHECK
CFLAGS_debug.o += -O0
CFLAGS_jnode.o += -O0
CFLAGS_znode.o += -O0
CFLAGS_key.o += -O0
CFLAGS_pool.o += -O0
CFLAGS_tree_mod.o += -O0
CFLAGS_carry.o += -O0
CFLAGS_carry_ops.o += -O0
CFLAGS_lock.o += -O0
CFLAGS_tree.o += -O0
CFLAGS_tap.o += -O0
CFLAGS_coord.o += -O0
CFLAGS_block_alloc.o += -O0
CFLAGS_txnmgr.o += -O0
CFLAGS_kassign.o += -O0
CFLAGS_flush.o += -O0
CFLAGS_wander.o += -O0
CFLAGS_eottl.o += -O0
CFLAGS_search.o += -O0
CFLAGS_blocknrset.o += -O0
CFLAGS_super.o += -O0
CFLAGS_tree_walk.o += -O0
CFLAGS_inode.o += -O0
CFLAGS_vfs_ops.o += -O0
CFLAGS_page_cache.o += -O0
CFLAGS_lnode.o += -O0
CFLAGS_kcond.o += -O0
CFLAGS_seal.o += -O0
CFLAGS_io_handle.o += -O0
CFLAGS_ktxnmgrd.o += -O0
CFLAGS_trace.o += -O0
CFLAGS_all-reiser4.o += -O0
endif
ifdef CONFIG_REISER4_ALL_IN_ONE
reiser4-objs := all-reiser4.o
else
reiser4-objs := \
	   debug.o \
	   jnode.o \
	   znode.o \
	   key.o \
	   pool.o \
	   tree_mod.o \
	   carry.o \
	   carry_ops.o \
	   lock.o \
	   tree.o \
	   tap.o \
	   coord.o \
	   block_alloc.o \
	   txnmgr.o \
	   kassign.o \
	   flush.o \
	   eottl.o \
	   wander.o \
	   search.o \
	   blocknrset.o \
	   super.o \
	   tree_walk.o \
	   inode.o \
	   vfs_ops.o \
	   page_cache.o \
	   lnode.o \
	   kcond.o \
	   seal.o \
	   io_handle.o \
	   trace.o \
	   ktxnmgrd.o

obj-$(CONFIG_REISER4_FS) += plugin/
endif

include $(TOPDIR)/Rules.make
