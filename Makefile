#
# Makefile for reiser4
#

O_TARGET := reiser4.o
obj-y    := debug.o key.o oid.o znode.o vfs_ops.o super.o coord.o lock.o plugin/file/file.o plugin/item/internal.o plugin/item/item.o plugin/item/static_stat.o plugin/node/node.o plugin/hash.o plugin/tail.o plugin/object.o plugin/plugin.o tree.o do_carry.o inode.o tree_walk.o cut.o

#           plugin/node/leaf40_node.o \
#           plugin/item/extent.o \
#           plugin/item/direct.o \
#           sys_reiser4.o \


obj-m    := $(O_TARGET)

include $(TOPDIR)/Rules.make
# DO NOT DELETE

debug.o: reiser4.h forward.h reiser4_sb.h build.h debug.h dformat.h key.h kassign.h coord.h plugin/item/static_stat.h plugin/item/internal.h plugin/item/sde.h plugin/item/cde.h plugin/item/extent.h plugin/item/tail.h plugin/file/file.h plugin/dir/hashed_dir.h plugin/dir/dir.h plugin/item/item.h plugin/node/node.h plugin/node/node40.h plugin/security/perm.h plugin/oid/oid_40.h plugin/oid/oid.h plugin/disk_format/layout_40.h plugin/disk_format/layout.h tshash.h tslist.h lnode.h plugin/plugin.h txnmgr.h znode.h slum.h block_alloc.h tree_walk.h pool.h tree_mod.h carry.h carry_ops.h tree.h inode.h super.h memory.h
znode.o: reiser4.h forward.h reiser4_sb.h build.h debug.h dformat.h key.h kassign.h coord.h plugin/item/static_stat.h plugin/item/internal.h plugin/item/sde.h plugin/item/cde.h plugin/item/extent.h plugin/item/tail.h plugin/file/file.h plugin/dir/hashed_dir.h plugin/dir/dir.h plugin/item/item.h plugin/node/node.h plugin/node/node40.h plugin/security/perm.h plugin/oid/oid_40.h plugin/oid/oid.h plugin/disk_format/layout_40.h plugin/disk_format/layout.h tshash.h tslist.h lnode.h plugin/plugin.h txnmgr.h znode.h slum.h block_alloc.h tree_walk.h pool.h tree_mod.h carry.h carry_ops.h tree.h inode.h super.h memory.h
key.o: reiser4.h forward.h reiser4_sb.h build.h debug.h dformat.h key.h kassign.h coord.h plugin/item/static_stat.h plugin/item/internal.h plugin/item/sde.h plugin/item/cde.h plugin/item/extent.h plugin/item/tail.h plugin/file/file.h plugin/dir/hashed_dir.h plugin/dir/dir.h plugin/item/item.h plugin/node/node.h plugin/node/node40.h plugin/security/perm.h plugin/oid/oid_40.h plugin/oid/oid.h plugin/disk_format/layout_40.h plugin/disk_format/layout.h tshash.h tslist.h lnode.h plugin/plugin.h txnmgr.h znode.h slum.h block_alloc.h tree_walk.h pool.h tree_mod.h carry.h carry_ops.h tree.h inode.h super.h memory.h
