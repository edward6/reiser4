#
# Makefile for reiser4
#

O_TARGET := reiser4.o
obj-y    := debug.o key.o oid.o znode.o vfs_ops.o super.o coords.o lock.o plugin/file/file.o plugin/item/internal.o plugin/item/item.o plugin/item/static_stat.o plugin/node/node.o plugin/hash.o plugin/tail.o plugin/object.o plugin/plugin.o tree.o do_carry.o inode.o tree_walk.o slum_track.o balance.o cut.o

#           plugin/node/leaf40_node.o \
#           plugin/item/extent.o \
#           plugin/item/direct.o \
#           sys_reiser4.o \


obj-m    := $(O_TARGET)

include $(TOPDIR)/Rules.make
