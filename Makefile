#
# reiser4/Makefile
#

obj-$(CONFIG_REISER4_FS) += reiser4.o

EXTRA_CFLAGS += \
           -Wformat \
	       -Wundef \
           -Wunused \
	       -Wcomment \
           \
	       -Wno-nested-externs \
	       -Wno-write-strings \
	       -Wno-sign-compare

#	       -Wpointer-arith \
#	       -Wlarger-than-16384 \
#	       -Winline \

ifeq ($(CONFIG_REISER4_NOOPT),y)
	EXTRA_CFLAGS += -O0
else
# this warning is only supported when optimization is on.
	EXTRA_CFLAGS += \
           -Wuninitialized
endif

reiser4-objs := \
		   debug.o \
		   stats.o \
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
		   oid.o \
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
		   prof.o \
		   repacker.o \
           \
		   plugin/plugin.o \
		   plugin/plugin_set.o \
		   plugin/plugin_hash.o \
		   plugin/node/node.o \
		   plugin/object.o \
		   plugin/symlink.o \
		   plugin/cryptcompress.o \
		   plugin/digest.o \
		   plugin/node/node40.o \
           \
		   plugin/item/static_stat.o \
		   plugin/item/sde.o \
		   plugin/item/cde.o \
		   plugin/item/internal.o \
		   plugin/item/tail.o \
		   plugin/item/ctail.o \
		   plugin/item/extent.o \
		   plugin/item/extent_item_ops.o \
		   plugin/item/extent_file_ops.o \
		   plugin/item/extent_flush_ops.o \
           \
		   plugin/hash.o \
		   plugin/tail_policy.o \
		   plugin/item/item.o \
           \
		   plugin/dir/hashed_dir.o \
		   plugin/dir/pseudo_dir.o \
		   plugin/dir/dir.o \
           \
		   plugin/security/perm.o \
		   plugin/security/acl.o \
           \
		   plugin/pseudo/pseudo.o \
           \
		   plugin/space/bitmap.o \
		   plugin/space/test.o \
		   plugin/space/space_allocator.o \
           \
		   plugin/disk_format/disk_format40.o \
		   plugin/disk_format/test.o \
		   plugin/disk_format/disk_format.o \
           \
		   plugin/file/pseudo.o \
		   plugin/file/file.o \
		   plugin/file/tail_conversion.o

reiser4-objs += sys_reiser4.o 
ifeq ($(CONFIG_REISER4_FS_SYSCALL),y)
YFLAGS= -d -v -r -b $(obj)/parser/parser
sys_reiser4.o: $/parser/parser.code.c $/parser/lib.c $/parser/pars.cls.h $/parser/parser.h $/parser/parser.y
#$(addprefix $(obj)/,$(reiser4-objs)): $(obj)/parser/parser.code.c
$(obj)/parser/parser.code.c: $(obj)/parser/parser.y
	$(YACC) $(YFLAGS) $(obj)/parser/parser.y

#	$(MAKE)  $(obj)/parser/parser
#clean-files := parser/parser.code.c
##clean-rule =@$(MAKE) -C $/parser clean
#clean-rule =@$(MAKE) $(obj)/parser/parser.code.c
endif

