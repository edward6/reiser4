cmd_fs/reiser4/as_ops.o := ccache cc -Wp,-MD,fs/reiser4/.as_ops.o.d -D__KERNEL__ -Iinclude -Wall -g -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -pipe -mpreferred-stack-boundary=2 -march=i686 -Iinclude/asm-i386/mach-default -g -nostdinc -iwithprefix include  -Wpointer-arith -Wwrite-strings -Woverloaded-virtual -Wformat -Wsynth -Wundef -Wcast-align -Wlarger-than-4096 -Wnon-virtual-dtor -Wreorder -Wsign-promo -Wuninitialized -Wunused -Wcomment -Wno-nested-externs -Wno-sign-compare  -DKBUILD_BASENAME=as_ops -DKBUILD_MODNAME=reiser4 -c -o fs/reiser4/as_ops.o fs/reiser4/as_ops.c

deps_fs/reiser4/as_ops.o := \
  fs/reiser4/as_ops.c \
  fs/reiser4/forward.h \
  include/asm/errno.h \
  include/asm-generic/errno.h \
  include/asm-generic/errno-base.h \
  fs/reiser4/debug.h \
    $(wildcard include/config/reiser4/debug.h) \
    $(wildcard include/config/reiser4/check/stack.h) \
    $(wildcard include/config/reiser4/debug/modify.h) \
    $(wildcard include/config/reiser4/debug/memcpy.h) \
    $(wildcard include/config/reiser4/debug/node.h) \
    $(wildcard include/config/reiser4/zero/new/node.h) \
    $(wildcard include/config/reiser4/trace.h) \
    $(wildcard include/config/reiser4/event/log.h) \
    $(wildcard include/config/reiser4/stats.h) \
    $(wildcard include/config/reiser4/debug/output.h) \
    $(wildcard include/config/reiser4/lockprof.h) \
    $(wildcard include/config/frame/pointer.h) \
    $(wildcard include/config/usermode.h) \
    $(wildcard include/config/reiser4/prof.h) \
  fs/reiser4/reiser4.h \
  include/asm/param.h \
  include/linux/errno.h \
  include/linux/types.h \
    $(wildcard include/config/uid16.h) \
  include/linux/config.h \
    $(wildcard include/config/h.h) \
  include/linux/posix_types.h \
  include/linux/stddef.h \
  include/asm/posix_types.h \
  include/asm/types.h \
    $(wildcard include/config/highmem.h) \
    $(wildcard include/config/lbd.h) \
  include/linux/fs.h \
    $(wildcard include/config/blk/dev/initrd.h) \
  include/linux/linkage.h \
  include/asm/linkage.h \
    $(wildcard include/config/x86/alignment/16.h) \
  include/linux/limits.h \
  include/linux/wait.h \
    $(wildcard include/config/smp.h) \
  include/linux/list.h \
  include/linux/prefetch.h \
  include/asm/processor.h \
    $(wildcard include/config/eisa.h) \
    $(wildcard include/config/x86/pc9800.h) \
    $(wildcard include/config/x86/prefetch.h) \
    $(wildcard include/config/x86/use/3dnow.h) \
  include/asm/vm86.h \
  include/asm/math_emu.h \
  include/asm/sigcontext.h \
  include/linux/compiler.h \
  include/asm/segment.h \
  include/asm/page.h \
    $(wildcard include/config/x86/pae.h) \
    $(wildcard include/config/hugetlb/page.h) \
    $(wildcard include/config/highmem4g.h) \
    $(wildcard include/config/highmem64g.h) \
    $(wildcard include/config/discontigmem.h) \
  include/asm/cpufeature.h \
  include/linux/bitops.h \
  include/asm/bitops.h \
  include/asm/msr.h \
  include/linux/cache.h \
  include/linux/kernel.h \
    $(wildcard include/config/debug/spinlock/sleep.h) \
  /usr/lib/gcc-lib/i486-suse-linux/2.95.3/include/stdarg.h \
  include/asm/byteorder.h \
    $(wildcard include/config/x86/bswap.h) \
  include/linux/byteorder/little_endian.h \
  include/linux/byteorder/swab.h \
  include/linux/byteorder/generic.h \
  include/asm/bug.h \
  include/asm/cache.h \
    $(wildcard include/config/x86/l1/cache/shift.h) \
  include/linux/threads.h \
    $(wildcard include/config/nr/cpus.h) \
  include/asm/system.h \
    $(wildcard include/config/x86/cmpxchg.h) \
    $(wildcard include/config/x86/sse2.h) \
    $(wildcard include/config/x86/oostore.h) \
  include/linux/spinlock.h \
    $(wildcard include/config/preempt.h) \
    $(wildcard include/config/debug/spinlock.h) \
  include/linux/preempt.h \
  include/linux/thread_info.h \
  include/asm/thread_info.h \
  include/linux/stringify.h \
  include/asm/spinlock.h \
    $(wildcard include/config/x86/ppro/fence.h) \
  include/asm/atomic.h \
  include/asm/rwlock.h \
  include/asm/current.h \
  include/linux/kdev_t.h \
  include/linux/ioctl.h \
  include/asm/ioctl.h \
  include/linux/dcache.h \
  include/linux/rcupdate.h \
  include/linux/percpu.h \
  include/linux/slab.h \
  include/linux/gfp.h \
  include/linux/mmzone.h \
    $(wildcard include/config/force/max/zoneorder.h) \
    $(wildcard include/config/numa.h) \
  include/asm/topology.h \
  include/asm-generic/topology.h \
  include/asm/percpu.h \
  include/asm-generic/percpu.h \
  include/linux/stat.h \
  include/asm/stat.h \
  include/linux/time.h \
  include/linux/seqlock.h \
  include/linux/timex.h \
  include/asm/timex.h \
    $(wildcard include/config/melan.h) \
    $(wildcard include/config/x86/tsc.h) \
  include/asm/div64.h \
  include/linux/radix-tree.h \
  include/linux/kobject.h \
  include/linux/sysfs.h \
  include/linux/rwsem.h \
    $(wildcard include/config/rwsem/generic/spinlock.h) \
  include/asm/rwsem.h \
  include/asm/semaphore.h \
  include/linux/quota.h \
  include/linux/dqblk_xfs.h \
  include/linux/dqblk_v1.h \
  include/linux/dqblk_v2.h \
  include/linux/nfs_fs_i.h \
  include/linux/nfs.h \
  include/linux/sunrpc/msg_prot.h \
  include/linux/fcntl.h \
  include/asm/fcntl.h \
  include/linux/err.h \
  include/asm/hardirq.h \
  include/linux/irq.h \
    $(wildcard include/config/arch/s390.h) \
  include/asm/irq.h \
    $(wildcard include/config/x86/local/apic.h) \
  include/linux/sched.h \
    $(wildcard include/config/security.h) \
  include/linux/capability.h \
  include/linux/jiffies.h \
  include/linux/rbtree.h \
  include/asm/ptrace.h \
  include/asm/mmu.h \
  include/linux/smp.h \
  include/asm/smp.h \
    $(wildcard include/config/x86/io/apic.h) \
  include/asm/fixmap.h \
    $(wildcard include/config/x86/visws/apic.h) \
    $(wildcard include/config/x86/f00f/bug.h) \
    $(wildcard include/config/x86/summit.h) \
    $(wildcard include/config/acpi/boot.h) \
  include/asm/acpi.h \
    $(wildcard include/config/acpi/sleep.h) \
  include/asm/apicdef.h \
  include/asm/kmap_types.h \
    $(wildcard include/config/debug/highmem.h) \
  include/asm/mpspec.h \
    $(wildcard include/config/x86/numaq.h) \
  include/asm/io_apic.h \
  include/asm/apic.h \
    $(wildcard include/config/x86/good/apic.h) \
    $(wildcard include/config/pm.h) \
  include/linux/pm.h \
  include/linux/sem.h \
  include/linux/ipc.h \
  include/asm/ipcbuf.h \
  include/asm/sembuf.h \
  include/linux/signal.h \
  include/asm/signal.h \
  include/asm/siginfo.h \
  include/asm-generic/siginfo.h \
  include/linux/string.h \
  include/asm/string.h \
  include/linux/securebits.h \
  include/linux/fs_struct.h \
  include/linux/completion.h \
  include/linux/pid.h \
  include/linux/param.h \
  include/linux/resource.h \
  include/asm/resource.h \
  include/linux/timer.h \
  include/linux/aio.h \
  include/linux/workqueue.h \
  include/linux/aio_abi.h \
  include/asm-i386/mach-default/irq_vectors.h \
  include/asm/hw_irq.h \
  include/linux/profile.h \
    $(wildcard include/config/profiling.h) \
  include/linux/init.h \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/hotplug.h) \
  include/linux/irq_cpustat.h \
  fs/reiser4/dformat.h \
  include/asm/unaligned.h \
  fs/reiser4/coord.h \
  fs/reiser4/plugin/item/item.h \
  fs/reiser4/plugin/item/../../forward.h \
  fs/reiser4/plugin/item/../plugin_header.h \
  fs/reiser4/plugin/item/../../tslist.h \
  fs/reiser4/plugin/item/../../debug.h \
  fs/reiser4/plugin/item/../../dformat.h \
  fs/reiser4/plugin/item/../../seal.h \
  fs/reiser4/plugin/item/../../key.h \
  fs/reiser4/plugin/item/../../coord.h \
  include/linux/mm.h \
    $(wildcard include/config/stack/growsup.h) \
    $(wildcard include/config/higmem.h) \
    $(wildcard include/config/pglock/debug.h) \
    $(wildcard include/config/mmu.h) \
  include/asm/pgtable.h \
    $(wildcard include/config/highpte.h) \
  include/asm/pgtable-2level.h \
  include/linux/page-flags.h \
    $(wildcard include/config/swap.h) \
  fs/reiser4/plugin/file/file.h \
  fs/reiser4/plugin/file/../../latch.h \
  fs/reiser4/plugin/file/../../kcond.h \
  fs/reiser4/plugin/security/perm.h \
  fs/reiser4/plugin/security/../../forward.h \
  fs/reiser4/plugin/security/../plugin_header.h \
  fs/reiser4/plugin/oid/oid.h \
  fs/reiser4/plugin/oid/../../forward.h \
  fs/reiser4/plugin/oid/../../key.h \
  fs/reiser4/plugin/oid/oid40.h \
  fs/reiser4/plugin/disk_format/disk_format.h \
  fs/reiser4/plugin/plugin.h \
  fs/reiser4/plugin/../forward.h \
  fs/reiser4/plugin/../debug.h \
  fs/reiser4/plugin/../dformat.h \
  fs/reiser4/plugin/../key.h \
  fs/reiser4/plugin/../tslist.h \
  fs/reiser4/plugin/plugin_header.h \
  fs/reiser4/plugin/item/static_stat.h \
  fs/reiser4/plugin/item/internal.h \
  fs/reiser4/plugin/item/sde.h \
  fs/reiser4/plugin/item/../../kassign.h \
  fs/reiser4/plugin/item/cde.h \
  fs/reiser4/plugin/item/extent.h \
  fs/reiser4/plugin/item/tail.h \
  fs/reiser4/plugin/pseudo/pseudo.h \
  fs/reiser4/plugin/pseudo/../plugin_header.h \
  fs/reiser4/plugin/pseudo/../../key.h \
  fs/reiser4/plugin/symlink.h \
  fs/reiser4/plugin/dir/hashed_dir.h \
  fs/reiser4/plugin/dir/../../forward.h \
  fs/reiser4/plugin/dir/dir.h \
  fs/reiser4/plugin/dir/../../kassign.h \
  fs/reiser4/plugin/node/node.h \
  fs/reiser4/plugin/node/../../forward.h \
  fs/reiser4/plugin/node/../../debug.h \
  fs/reiser4/plugin/node/../../dformat.h \
  fs/reiser4/plugin/node/../plugin_header.h \
  fs/reiser4/plugin/node/node40.h \
  fs/reiser4/plugin/space/bitmap.h \
  fs/reiser4/plugin/space/../../dformat.h \
  fs/reiser4/plugin/space/space_allocator.h \
  fs/reiser4/plugin/space/../../forward.h \
  fs/reiser4/plugin/space/test.h \
  fs/reiser4/plugin/space/../../block_alloc.h \
  fs/reiser4/plugin/disk_format/disk_format40.h \
  fs/reiser4/plugin/disk_format/../../dformat.h \
  fs/reiser4/plugin/disk_format/test.h \
  fs/reiser4/plugin/disk_format/../../key.h \
  include/linux/buffer_head.h \
  fs/reiser4/plugin/plugin_set.h \
  fs/reiser4/plugin/../tshash.h \
  fs/reiser4/plugin/plugin_hash.h \
  fs/reiser4/plugin/object.h \
  fs/reiser4/txnmgr.h \
  fs/reiser4/spin_macros.h \
  fs/reiser4/spinprof.h \
  fs/reiser4/tslist.h \
  fs/reiser4/jnode.h \
  fs/reiser4/tshash.h \
  fs/reiser4/key.h \
  fs/reiser4/emergency_flush.h \
  fs/reiser4/block_alloc.h \
  fs/reiser4/znode.h \
  fs/reiser4/lock.h \
  fs/reiser4/readahead.h \
  include/linux/pagemap.h \
  include/linux/highmem.h \
  include/asm/cacheflush.h \
  include/asm/highmem.h \
  include/linux/interrupt.h \
  include/asm/tlbflush.h \
    $(wildcard include/config/x86/invlpg.h) \
  include/asm/uaccess.h \
    $(wildcard include/config/x86/intel/usercopy.h) \
    $(wildcard include/config/x86/wp/works/ok.h) \
  fs/reiser4/tree.h \
  fs/reiser4/tap.h \
  fs/reiser4/trace.h \
  fs/reiser4/vfs_ops.h \
  fs/reiser4/seal.h \
  fs/reiser4/inode.h \
  fs/reiser4/kcond.h \
  fs/reiser4/scint.h \
  fs/reiser4/page_cache.h \
  fs/reiser4/ktxnmgrd.h \
  fs/reiser4/super.h \
    $(wildcard include/config/reiser4/badblocks.h) \
  fs/reiser4/context.h \
  fs/reiser4/lnode.h \
  fs/reiser4/entd.h \
  fs/reiser4/kattr.h \
  include/linux/mount.h \
  include/linux/vfs.h \
  include/asm/statfs.h \
  include/linux/seq_file.h \
  include/linux/module.h \
    $(wildcard include/config/modversions.h) \
    $(wildcard include/config/module/unload.h) \
    $(wildcard include/config/kallsyms.h) \
  include/linux/kmod.h \
    $(wildcard include/config/kmod.h) \
  include/linux/elf.h \
  include/asm/elf.h \
  include/asm/user.h \
  include/linux/utsname.h \
  include/asm/module.h \
    $(wildcard include/config/m386.h) \
    $(wildcard include/config/m486.h) \
    $(wildcard include/config/m586.h) \
    $(wildcard include/config/m586tsc.h) \
    $(wildcard include/config/m586mmx.h) \
    $(wildcard include/config/m686.h) \
    $(wildcard include/config/mpentiumii.h) \
    $(wildcard include/config/mpentiumiii.h) \
    $(wildcard include/config/mpentium4.h) \
    $(wildcard include/config/mk6.h) \
    $(wildcard include/config/mk7.h) \
    $(wildcard include/config/mk8.h) \
    $(wildcard include/config/mcrusoe.h) \
    $(wildcard include/config/mwinchipc6.h) \
    $(wildcard include/config/mwinchip2.h) \
    $(wildcard include/config/mwinchip3d.h) \
    $(wildcard include/config/mcyrixiii.h) \
    $(wildcard include/config/mviac3/2.h) \
  include/linux/writeback.h \
  include/linux/mpage.h \
  include/linux/backing-dev.h \
  include/linux/quotaops.h \
    $(wildcard include/config/quota.h) \
  include/linux/smp_lock.h \
  include/linux/security.h \
    $(wildcard include/config/security/network.h) \
  include/linux/binfmts.h \
  include/linux/sysctl.h \
  include/linux/shm.h \
  include/asm/shmparam.h \
  include/asm/shmbuf.h \
  include/linux/msg.h \
  include/asm/msgbuf.h \
  include/linux/skbuff.h \
    $(wildcard include/config/netfilter.h) \
    $(wildcard include/config/bridge.h) \
    $(wildcard include/config/bridge/module.h) \
    $(wildcard include/config/netfilter/debug.h) \
    $(wildcard include/config/hippi.h) \
    $(wildcard include/config/net/sched.h) \
  include/linux/poll.h \
  include/asm/poll.h \
  include/linux/net.h \
  include/linux/netlink.h \
  include/linux/socket.h \
    $(wildcard include/config/compat.h) \
  include/asm/socket.h \
  include/asm/sockios.h \
  include/linux/sockios.h \
  include/linux/uio.h \

fs/reiser4/as_ops.o: $(deps_fs/reiser4/as_ops.o)

$(deps_fs/reiser4/as_ops.o):
