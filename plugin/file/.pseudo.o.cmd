cmd_fs/reiser4/plugin/file/pseudo.o := ccache /usr/bin/cc -Wp,-MD,fs/reiser4/plugin/file/.pseudo.o.d -D__KERNEL__ -Iinclude -Wall -Wstrict-prototypes -Wno-trigraphs -O2 -fno-strict-aliasing -fno-common -U__i386__ -Ui386 -g -D__arch_um__ -DSUBARCH=\"i386\" -D_LARGEFILE64_SOURCE -Iarch/um/include -Derrno=kernel_errno -Dsigprocmask=kernel_sigprocmask -I/home/god/projects/2.5/arch/um/kernel/tt/include -nostdinc -iwithprefix include  -Wpointer-arith -Wformat -Wundef -Wcast-align -Wlarger-than-16384 -Wunused -Wcomment -Wno-nested-externs -Wno-write-strings -Wno-sign-compare -Wuninitialized  -DKBUILD_BASENAME=pseudo -DKBUILD_MODNAME=reiser4 -c -o fs/reiser4/plugin/file/pseudo.o fs/reiser4/plugin/file/pseudo.c

deps_fs/reiser4/plugin/file/pseudo.o := \
  fs/reiser4/plugin/file/pseudo.c \
  fs/reiser4/plugin/file/pseudo.h \
  include/linux/fs.h \
    $(wildcard include/config/blk/dev/initrd.h) \
  include/linux/config.h \
    $(wildcard include/config/h.h) \
  include/linux/linkage.h \
  include/asm/linkage.h \
  include/linux/limits.h \
  include/linux/wait.h \
  include/linux/list.h \
  include/linux/stddef.h \
  include/linux/prefetch.h \
  include/asm/processor.h \
  include/asm/arch/user.h \
  include/asm/page.h \
  include/asm/arch/page.h \
    $(wildcard include/config/x86/use/3dnow.h) \
    $(wildcard include/config/x86/pae.h) \
    $(wildcard include/config/hugetlb/page.h) \
    $(wildcard include/config/highmem4g.h) \
    $(wildcard include/config/highmem64g.h) \
    $(wildcard include/config/discontigmem.h) \
  include/asm/processor-generic.h \
    $(wildcard include/config/mode/tt.h) \
    $(wildcard include/config/mode/skas.h) \
    $(wildcard include/config/kernel/stack/order.h) \
    $(wildcard include/config/smp.h) \
  include/asm/ptrace.h \
  arch/um/include/sysdep/ptrace.h \
  arch/um/include/uml-config.h \
    $(wildcard include/config/usermode.h) \
    $(wildcard include/config/mmu.h) \
    $(wildcard include/config/uid16.h) \
    $(wildcard include/config/rwsem/generic/spinlock.h) \
    $(wildcard include/config/experimental.h) \
    $(wildcard include/config/swap.h) \
    $(wildcard include/config/sysvipc.h) \
    $(wildcard include/config/bsd/process/acct.h) \
    $(wildcard include/config/sysctl.h) \
    $(wildcard include/config/log/buf/shift.h) \
    $(wildcard include/config/embedded.h) \
    $(wildcard include/config/futex.h) \
    $(wildcard include/config/epoll.h) \
    $(wildcard include/config/modules.h) \
    $(wildcard include/config/net.h) \
    $(wildcard include/config/hostfs.h) \
    $(wildcard include/config/mconsole.h) \
    $(wildcard include/config/host/2g/2g.h) \
    $(wildcard include/config/uml/smp.h) \
    $(wildcard include/config/nest/level.h) \
    $(wildcard include/config/kernel/half/gigs.h) \
    $(wildcard include/config/highmem.h) \
    $(wildcard include/config/proc/mm.h) \
    $(wildcard include/config/binfmt/aout.h) \
    $(wildcard include/config/binfmt/elf.h) \
    $(wildcard include/config/binfmt/misc.h) \
    $(wildcard include/config/stdio/console.h) \
    $(wildcard include/config/ssl.h) \
    $(wildcard include/config/fd/chan.h) \
    $(wildcard include/config/null/chan.h) \
    $(wildcard include/config/port/chan.h) \
    $(wildcard include/config/pty/chan.h) \
    $(wildcard include/config/tty/chan.h) \
    $(wildcard include/config/xterm/chan.h) \
    $(wildcard include/config/con/zero/chan.h) \
    $(wildcard include/config/con/chan.h) \
    $(wildcard include/config/ssl/chan.h) \
    $(wildcard include/config/unix98/ptys.h) \
    $(wildcard include/config/unix98/pty/count.h) \
    $(wildcard include/config/watchdog.h) \
    $(wildcard include/config/uml/sound.h) \
    $(wildcard include/config/sound.h) \
    $(wildcard include/config/hostaudio.h) \
    $(wildcard include/config/blk/dev/ubd.h) \
    $(wildcard include/config/blk/dev/ubd/sync.h) \
    $(wildcard include/config/blk/dev/loop.h) \
    $(wildcard include/config/blk/dev/nbd.h) \
    $(wildcard include/config/blk/dev/ram.h) \
    $(wildcard include/config/mmapper.h) \
    $(wildcard include/config/netdevices.h) \
    $(wildcard include/config/packet.h) \
    $(wildcard include/config/packet/mmap.h) \
    $(wildcard include/config/netlink/dev.h) \
    $(wildcard include/config/netfilter.h) \
    $(wildcard include/config/unix.h) \
    $(wildcard include/config/net/key.h) \
    $(wildcard include/config/inet.h) \
    $(wildcard include/config/ip/multicast.h) \
    $(wildcard include/config/ip/advanced/router.h) \
    $(wildcard include/config/ip/pnp.h) \
    $(wildcard include/config/net/ipip.h) \
    $(wildcard include/config/net/ipgre.h) \
    $(wildcard include/config/arpd.h) \
    $(wildcard include/config/inet/ecn.h) \
    $(wildcard include/config/syn/cookies.h) \
    $(wildcard include/config/inet/ah.h) \
    $(wildcard include/config/inet/esp.h) \
    $(wildcard include/config/inet/ipcomp.h) \
    $(wildcard include/config/ipv6.h) \
    $(wildcard include/config/xfrm/user.h) \
    $(wildcard include/config/ipv6/sctp//.h) \
    $(wildcard include/config/ip/sctp.h) \
    $(wildcard include/config/atm.h) \
    $(wildcard include/config/vlan/8021q.h) \
    $(wildcard include/config/llc.h) \
    $(wildcard include/config/decnet.h) \
    $(wildcard include/config/bridge.h) \
    $(wildcard include/config/x25.h) \
    $(wildcard include/config/lapb.h) \
    $(wildcard include/config/net/divert.h) \
    $(wildcard include/config/econet.h) \
    $(wildcard include/config/wan/router.h) \
    $(wildcard include/config/net/fastroute.h) \
    $(wildcard include/config/net/hw/flowcontrol.h) \
    $(wildcard include/config/net/sched.h) \
    $(wildcard include/config/net/pktgen.h) \
    $(wildcard include/config/dummy.h) \
    $(wildcard include/config/bonding.h) \
    $(wildcard include/config/equalizer.h) \
    $(wildcard include/config/tun.h) \
    $(wildcard include/config/ethertap.h) \
    $(wildcard include/config/net/ethernet.h) \
    $(wildcard include/config/ppp.h) \
    $(wildcard include/config/ppp/multilink.h) \
    $(wildcard include/config/ppp/filter.h) \
    $(wildcard include/config/ppp/async.h) \
    $(wildcard include/config/ppp/sync/tty.h) \
    $(wildcard include/config/ppp/deflate.h) \
    $(wildcard include/config/ppp/bsdcomp.h) \
    $(wildcard include/config/pppoe.h) \
    $(wildcard include/config/slip.h) \
    $(wildcard include/config/slip/compressed.h) \
    $(wildcard include/config/slip/smart.h) \
    $(wildcard include/config/slip/mode/slip6.h) \
    $(wildcard include/config/net/radio.h) \
    $(wildcard include/config/shaper.h) \
    $(wildcard include/config/wan.h) \
    $(wildcard include/config/uml/net.h) \
    $(wildcard include/config/ext2/fs.h) \
    $(wildcard include/config/ext2/fs/xattr.h) \
    $(wildcard include/config/ext3/fs.h) \
    $(wildcard include/config/jbd.h) \
    $(wildcard include/config/reiser4/fs.h) \
    $(wildcard include/config/reiser4/fs/syscall.h) \
    $(wildcard include/config/reiser4/check.h) \
    $(wildcard include/config/reiser4/debug.h) \
    $(wildcard include/config/reiser4/fs/syscall/debug.h) \
    $(wildcard include/config/reiser4/debug/modify.h) \
    $(wildcard include/config/reiser4/debug/memcpy.h) \
    $(wildcard include/config/reiser4/debug/node.h) \
    $(wildcard include/config/reiser4/zero/new/node.h) \
    $(wildcard include/config/reiser4/trace.h) \
    $(wildcard include/config/reiser4/event/log.h) \
    $(wildcard include/config/reiser4/stats.h) \
    $(wildcard include/config/reiser4/prof.h) \
    $(wildcard include/config/reiser4/lockprof.h) \
    $(wildcard include/config/reiser4/debug/output.h) \
    $(wildcard include/config/reiser4/noopt.h) \
    $(wildcard include/config/reiser4/use/eflush.h) \
    $(wildcard include/config/reiser4/badblocks.h) \
    $(wildcard include/config/reiserfs/fs.h) \
    $(wildcard include/config/jfs/fs.h) \
    $(wildcard include/config/xfs/fs.h) \
    $(wildcard include/config/minix/fs.h) \
    $(wildcard include/config/romfs/fs.h) \
    $(wildcard include/config/quota.h) \
    $(wildcard include/config/autofs/fs.h) \
    $(wildcard include/config/autofs4/fs.h) \
    $(wildcard include/config/iso9660/fs.h) \
    $(wildcard include/config/udf/fs.h) \
    $(wildcard include/config/fat/fs.h) \
    $(wildcard include/config/ntfs/fs.h) \
    $(wildcard include/config/proc/fs.h) \
    $(wildcard include/config/devfs/fs.h) \
    $(wildcard include/config/devpts/fs.h) \
    $(wildcard include/config/devpts/fs/xattr.h) \
    $(wildcard include/config/tmpfs.h) \
    $(wildcard include/config/ramfs.h) \
    $(wildcard include/config/adfs/fs.h) \
    $(wildcard include/config/affs/fs.h) \
    $(wildcard include/config/hfs/fs.h) \
    $(wildcard include/config/befs/fs.h) \
    $(wildcard include/config/bfs/fs.h) \
    $(wildcard include/config/efs/fs.h) \
    $(wildcard include/config/cramfs.h) \
    $(wildcard include/config/vxfs/fs.h) \
    $(wildcard include/config/hpfs/fs.h) \
    $(wildcard include/config/qnx4fs/fs.h) \
    $(wildcard include/config/sysv/fs.h) \
    $(wildcard include/config/ufs/fs.h) \
    $(wildcard include/config/nfs/fs.h) \
    $(wildcard include/config/nfsd.h) \
    $(wildcard include/config/exportfs.h) \
    $(wildcard include/config/smb/fs.h) \
    $(wildcard include/config/cifs.h) \
    $(wildcard include/config/ncp/fs.h) \
    $(wildcard include/config/coda/fs.h) \
    $(wildcard include/config/intermezzo/fs.h) \
    $(wildcard include/config/afs/fs.h) \
    $(wildcard include/config/partition/advanced.h) \
    $(wildcard include/config/msdos/partition.h) \
    $(wildcard include/config/security.h) \
    $(wildcard include/config/crypto.h) \
    $(wildcard include/config/crc32.h) \
    $(wildcard include/config/scsi.h) \
    $(wildcard include/config/md.h) \
    $(wildcard include/config/mtd.h) \
    $(wildcard include/config/profiling.h) \
    $(wildcard include/config/debug/slab.h) \
    $(wildcard include/config/debug/spinlock.h) \
    $(wildcard include/config/debugsym.h) \
    $(wildcard include/config/frame/pointer.h) \
    $(wildcard include/config/pt/proxy.h) \
    $(wildcard include/config/gprof.h) \
    $(wildcard include/config/gcov.h) \
  /home/god/projects/2.5/arch/um/kernel/tt/include/ptrace-tt.h \
  arch/um/include/sysdep/sc.h \
  arch/um/include/choose-mode.h \
  include/asm/ptrace-generic.h \
  include/asm/current.h \
  include/asm/arch/ptrace.h \
  arch/um/include/skas_ptrace.h \
  include/asm/cache.h \
  include/asm/system.h \
  include/asm/system-generic.h \
  include/asm/arch/system.h \
    $(wildcard include/config/x86/cmpxchg.h) \
    $(wildcard include/config/x86/oostore.h) \
  include/linux/kernel.h \
    $(wildcard include/config/debug/spinlock/sleep.h) \
  /usr/lib/gcc-lib/i486-suse-linux/2.95.3/include/stdarg.h \
  include/linux/types.h \
  include/linux/posix_types.h \
  include/asm/posix_types.h \
  include/asm/arch/posix_types.h \
  include/asm/types.h \
  include/asm/arch/types.h \
    $(wildcard include/config/lbd.h) \
  include/linux/compiler.h \
  include/asm/byteorder.h \
  include/asm/arch/byteorder.h \
    $(wildcard include/config/x86/bswap.h) \
  include/linux/byteorder/little_endian.h \
  include/linux/byteorder/swab.h \
  include/linux/byteorder/generic.h \
  include/asm/bug.h \
  include/asm/arch/bug.h \
  include/asm/segment.h \
  include/asm/cpufeature.h \
  include/asm/arch/cpufeature.h \
  include/linux/bitops.h \
  include/asm/bitops.h \
  include/asm/arch/bitops.h \
  include/linux/spinlock.h \
    $(wildcard include/config/preempt.h) \
  include/linux/preempt.h \
  include/linux/thread_info.h \
  include/asm/thread_info.h \
  include/linux/stringify.h \
  include/linux/kdev_t.h \
  include/linux/ioctl.h \
  include/asm/ioctl.h \
  include/asm/arch/ioctl.h \
  include/linux/dcache.h \
  include/asm/atomic.h \
  include/asm/arch/atomic.h \
  include/linux/cache.h \
  include/linux/rcupdate.h \
  include/linux/threads.h \
    $(wildcard include/config/nr/cpus.h) \
  include/linux/percpu.h \
  include/linux/slab.h \
  include/linux/gfp.h \
  include/linux/mmzone.h \
    $(wildcard include/config/force/max/zoneorder.h) \
    $(wildcard include/config/numa.h) \
  include/linux/topology.h \
  include/linux/smp.h \
  include/asm/topology.h \
  include/asm-generic/topology.h \
  include/linux/kmalloc_sizes.h \
    $(wildcard include/config/large/allocs.h) \
  include/linux/string.h \
  include/asm/string.h \
  include/asm/arch/string.h \
  include/asm/archparam.h \
  include/asm/user.h \
  include/asm/percpu.h \
  include/asm/arch/percpu.h \
  include/asm-generic/percpu.h \
  include/linux/stat.h \
  include/asm/stat.h \
  include/asm/arch/stat.h \
  include/linux/time.h \
  include/asm/param.h \
  include/linux/seqlock.h \
  include/linux/timex.h \
    $(wildcard include/config/time/interpolation.h) \
  include/asm/timex.h \
  include/asm/div64.h \
  include/asm/arch/div64.h \
  include/linux/radix-tree.h \
  include/linux/kobject.h \
  include/linux/sysfs.h \
  include/linux/rwsem.h \
  include/linux/rwsem-spinlock.h \
  include/asm/semaphore.h \
  include/asm/arch/semaphore.h \
  include/linux/quota.h \
  include/linux/errno.h \
  include/asm/errno.h \
  include/asm/arch/errno.h \
  include/asm-generic/errno.h \
  include/asm-generic/errno-base.h \
  include/linux/dqblk_xfs.h \
  include/linux/dqblk_v1.h \
  include/linux/dqblk_v2.h \
  include/linux/nfs_fs_i.h \
  include/linux/nfs.h \
  include/linux/sunrpc/msg_prot.h \
  include/linux/fcntl.h \
  include/asm/fcntl.h \
  include/asm/arch/fcntl.h \
  include/linux/err.h \
  fs/reiser4/plugin/file/../plugin.h \
  fs/reiser4/plugin/file/../../forward.h \
  fs/reiser4/plugin/file/../../debug.h \
  fs/reiser4/plugin/file/../../reiser4.h \
  include/asm/hardirq.h \
  include/asm/arch/hardirq.h \
  include/linux/irq.h \
    $(wildcard include/config/arch/s390.h) \
  include/asm/irq.h \
  include/asm/hw_irq.h \
  include/linux/irq_cpustat.h \
  include/linux/sched.h \
  include/linux/capability.h \
  include/linux/jiffies.h \
  include/linux/rbtree.h \
  include/asm/mmu.h \
  arch/um/include/um_mmu.h \
  arch/um/include/../kernel/tt/include/mmu.h \
  include/linux/sem.h \
  include/linux/ipc.h \
  include/asm/ipcbuf.h \
  include/asm/arch/ipcbuf.h \
  include/asm/sembuf.h \
  include/asm/arch/sembuf.h \
  include/linux/signal.h \
  include/asm/signal.h \
  include/asm/arch/signal.h \
  include/asm/sigcontext.h \
  include/asm/sigcontext-generic.h \
  include/asm/arch/sigcontext.h \
  include/asm/siginfo.h \
  include/asm/arch/siginfo.h \
  include/asm-generic/siginfo.h \
  include/linux/securebits.h \
  include/linux/fs_struct.h \
  include/linux/completion.h \
  include/linux/pid.h \
  include/linux/param.h \
  include/linux/resource.h \
  include/asm/resource.h \
  include/asm/arch/resource.h \
  include/linux/timer.h \
  include/linux/aio.h \
  include/linux/workqueue.h \
  include/linux/aio_abi.h \
  fs/reiser4/plugin/file/../../dformat.h \
  include/asm/unaligned.h \
  include/asm/arch/unaligned.h \
  fs/reiser4/plugin/file/../../key.h \
  fs/reiser4/plugin/file/../../tslist.h \
  fs/reiser4/plugin/file/../plugin_header.h \
  fs/reiser4/plugin/file/../item/static_stat.h \
  fs/reiser4/plugin/file/../item/../../forward.h \
  fs/reiser4/plugin/file/../item/../../dformat.h \
  fs/reiser4/plugin/file/../item/internal.h \
  fs/reiser4/plugin/file/../item/sde.h \
  fs/reiser4/plugin/file/../item/../../kassign.h \
  fs/reiser4/plugin/file/../item/../../key.h \
  fs/reiser4/plugin/file/../item/cde.h \
  fs/reiser4/plugin/file/../file/file.h \
  fs/reiser4/plugin/file/../file/../../latch.h \
  fs/reiser4/plugin/file/../file/../../kcond.h \
  fs/reiser4/plugin/file/../file/../../coord.h \
  fs/reiser4/plugin/file/../file/../../forward.h \
  fs/reiser4/plugin/file/../file/../../debug.h \
  fs/reiser4/plugin/file/../file/../../dformat.h \
  fs/reiser4/plugin/file/../file/../item/extent.h \
  fs/reiser4/plugin/file/../file/../item/tail.h \
  fs/reiser4/plugin/file/../file/../../seal.h \
  fs/reiser4/plugin/file/../file/../../key.h \
  fs/reiser4/plugin/file/../pseudo/pseudo.h \
  fs/reiser4/plugin/file/../pseudo/../plugin_header.h \
  fs/reiser4/plugin/file/../pseudo/../../key.h \
  include/linux/seq_file.h \
  fs/reiser4/plugin/file/../symlink.h \
  fs/reiser4/plugin/file/../dir/hashed_dir.h \
  fs/reiser4/plugin/file/../dir/../../forward.h \
  fs/reiser4/plugin/file/../dir/dir.h \
  fs/reiser4/plugin/file/../dir/../../kassign.h \
  fs/reiser4/plugin/file/../item/item.h \
  fs/reiser4/plugin/file/../item/../plugin_header.h \
  fs/reiser4/plugin/file/../item/../../seal.h \
  fs/reiser4/plugin/file/../item/../../plugin/file/file.h \
  include/linux/mm.h \
    $(wildcard include/config/stack/growsup.h) \
    $(wildcard include/config/higmem.h) \
  include/asm/pgtable.h \
    $(wildcard include/config/highpte.h) \
  include/asm/fixmap.h \
  include/asm/kmap_types.h \
  include/asm/arch/kmap_types.h \
    $(wildcard include/config/debug/highmem.h) \
  include/asm-generic/pgtable.h \
  include/linux/page-flags.h \
  fs/reiser4/plugin/file/../node/node.h \
  fs/reiser4/plugin/file/../node/../../forward.h \
  fs/reiser4/plugin/file/../node/../../debug.h \
  fs/reiser4/plugin/file/../node/../../dformat.h \
  fs/reiser4/plugin/file/../node/../plugin_header.h \
  fs/reiser4/plugin/file/../node/node40.h \
  fs/reiser4/plugin/file/../security/perm.h \
  fs/reiser4/plugin/file/../security/../../forward.h \
  fs/reiser4/plugin/file/../security/../plugin_header.h \
  fs/reiser4/plugin/file/../space/bitmap.h \
  fs/reiser4/plugin/file/../space/../../dformat.h \
  fs/reiser4/plugin/file/../space/space_allocator.h \
  fs/reiser4/plugin/file/../space/../../forward.h \
  fs/reiser4/plugin/file/../space/test.h \
  fs/reiser4/plugin/file/../space/../../block_alloc.h \
  fs/reiser4/plugin/file/../disk_format/disk_format40.h \
  fs/reiser4/plugin/file/../disk_format/../../dformat.h \
  fs/reiser4/plugin/file/../disk_format/test.h \
  fs/reiser4/plugin/file/../disk_format/../../key.h \
  fs/reiser4/plugin/file/../disk_format/disk_format.h \
  include/linux/buffer_head.h \
  fs/reiser4/plugin/file/../../inode.h \
  fs/reiser4/plugin/file/../../spin_macros.h \
  include/linux/profile.h \
  include/linux/init.h \
    $(wildcard include/config/hotplug.h) \
  fs/reiser4/plugin/file/../../spinprof.h \
  fs/reiser4/plugin/file/../../statcnt.h \
  fs/reiser4/plugin/file/../../kcond.h \
  fs/reiser4/plugin/file/../../seal.h \
  fs/reiser4/plugin/file/../../scint.h \
  fs/reiser4/plugin/file/../../plugin/plugin.h \
  fs/reiser4/plugin/file/../../plugin/cryptcompress.h \
  include/linux/pagemap.h \
  include/linux/highmem.h \
  include/asm/cacheflush.h \
  include/asm/arch/cacheflush.h \
  include/asm/uaccess.h \
  arch/um/include/um_uaccess.h \
  arch/um/include/../kernel/tt/include/uaccess.h \
  include/asm/a.out.h \
  include/asm/arch/a.out.h \
  arch/um/include/uml_uaccess.h \
  fs/reiser4/plugin/file/../../plugin/plugin_set.h \
  fs/reiser4/plugin/file/../../plugin/../tshash.h \
  fs/reiser4/plugin/file/../../plugin/../debug.h \
  fs/reiser4/plugin/file/../../plugin/../stats.h \
  fs/reiser4/plugin/file/../../plugin/../forward.h \
  fs/reiser4/plugin/file/../../plugin/../reiser4.h \
  fs/reiser4/plugin/file/../../plugin/../statcnt.h \
  fs/reiser4/plugin/file/../../plugin/security/perm.h \
  fs/reiser4/plugin/file/../../plugin/pseudo/pseudo.h \
  fs/reiser4/plugin/file/../../vfs_ops.h \
  fs/reiser4/plugin/file/../../coord.h \
  fs/reiser4/plugin/file/../../plugin/dir/dir.h \
  fs/reiser4/plugin/file/../../plugin/file/file.h \
  fs/reiser4/plugin/file/../../super.h \
  fs/reiser4/plugin/file/../../tree.h \
  fs/reiser4/plugin/file/../../plugin/node/node.h \
  fs/reiser4/plugin/file/../../jnode.h \
  fs/reiser4/plugin/file/../../tshash.h \
  fs/reiser4/plugin/file/../../txnmgr.h \
  fs/reiser4/plugin/file/../../emergency_flush.h \
  fs/reiser4/plugin/file/../../block_alloc.h \
  fs/reiser4/plugin/file/../../znode.h \
  fs/reiser4/plugin/file/../../lock.h \
  fs/reiser4/plugin/file/../../readahead.h \
  fs/reiser4/plugin/file/../../tap.h \
  fs/reiser4/plugin/file/../../context.h \
  fs/reiser4/plugin/file/../../trace.h \
  fs/reiser4/plugin/file/../../lnode.h \
  fs/reiser4/plugin/file/../../plugin/plugin_header.h \
  fs/reiser4/plugin/file/../../entd.h \
  fs/reiser4/plugin/file/../../prof.h \
  fs/reiser4/plugin/file/../../kattr.h \
  include/asm-i386/msr.h \
  fs/reiser4/plugin/file/../../plugin/space/space_allocator.h \
  fs/reiser4/plugin/file/../../plugin/disk_format/test.h \
  fs/reiser4/plugin/file/../../plugin/disk_format/disk_format40.h \

fs/reiser4/plugin/file/pseudo.o: $(deps_fs/reiser4/plugin/file/pseudo.o)

$(deps_fs/reiser4/plugin/file/pseudo.o):
