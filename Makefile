#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o

reiser4-objs := debug.o znode.o key.o pool.o tree_mod.o carry.o carry_ops.o \
	  lock.o tree.o coords.o block_alloc.o txnmgr.o kassign.o \
	  flush.o wander.o search.o blocknrset.o super.o tree_walk.o \
	  inode.o vfs_ops.o memory.o lnode.o kcond.o seal.o

obj-$(CONFIG_REISER4_FS) += plugin/

