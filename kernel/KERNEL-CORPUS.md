# CRUG kernel object corpus

**Document type:** Pinned corpus definition for Phase 19 stages K4 and K7
**Status:** Accepted
**Owner:** CRUG project owner (compiler lead). The stage scripts are the adjudicator.
**Derived from:** `docs/crug/KERNEL-BASELINE.md` (the pinned Linux 6.18.57 tree),
`docs/crug/EXIT-GATES.md` phase 19 stage K4 and its `VAGUE` item, and
`spec/CRUG_PRODUCTION_ROADMAP.md` section 24 (stages K3, K4 and K7).

Phase 19's gate asks for one subsystem (K3), a "substantial cross-section"
(K4) and a second-architecture subset (K7). This file defines all three sizes so
that the claim is a list, not an adjective:

- K4 is exactly the object list in section 2, 438 objects, compiled from source
  with `crugc` against the pinned tree and the pinned `x86_64_defconfig`.
- K7 is exactly the object list in section 3, 15 objects, compiled from source
  with a cross `crugc` for `aarch64-linux-gnu` against the same tree and
  `defconfig` selected with `ARCH=arm64`.
- K3 stays `lib/` as a whole, which is the 106 objects of section 2 that live
  directly in `lib/`.

The lists are generated, not hand-picked: membership is "every object kbuild
produces directly in the listed directories for the pinned configuration". A
different configuration, tree or directory set changes the list, so a changed
list is a corpus change and is recorded as an amendment here.

## 1. Relationship to the pin

The corpus never enters the repository. `KERNEL-BASELINE.md` section 6 keeps
the kernel source out of git; this file pins only the object names, which are
stable text derived from that source, and the counts. `scripts/kernel-stage.sh`
refuses to build unless the tree reports Linux 6.18.57 and the tarball digest in
`sources/kernel` still matches `docs/crug/kernel-baseline.sha256`.

Regenerate the list after any change to the pin, configuration or directory set:

```sh
for d in arch/x86/mm drivers/base fs init ipc kernel lib mm net/core; do
    find "build/kernel-crug/$d" -maxdepth 1 -name '*.o' -type f
done | sed 's|.*/kernel-crug/||' | sort
```

## 2. x86_64 corpus (stage K4)

438 objects in nine directories, all built with `CC=gcc/crugc`:

| Directory | Objects |
|-----------|---------|
| `arch/x86/mm/` | 20 |
| `drivers/base/` | 29 |
| `fs/` | 65 |
| `init/` | 8 |
| `ipc/` | 11 |
| `kernel/` | 76 |
| `lib/` | 106 |
| `mm/` | 75 |
| `net/core/` | 48 |
| **Total** | **438** |

```
<!-- corpus:begin -->
arch/x86/mm/amdtopology.o
arch/x86/mm/cpu_entry_area.o
arch/x86/mm/dump_pagetables.o
arch/x86/mm/extable.o
arch/x86/mm/fault.o
arch/x86/mm/hugetlbpage.o
arch/x86/mm/init_64.o
arch/x86/mm/init.o
arch/x86/mm/ioremap.o
arch/x86/mm/kaslr.o
arch/x86/mm/maccess.o
arch/x86/mm/mmap.o
arch/x86/mm/numa.o
arch/x86/mm/pgprot.o
arch/x86/mm/pgtable.o
arch/x86/mm/physaddr.o
arch/x86/mm/pkeys.o
arch/x86/mm/pti.o
arch/x86/mm/srat.o
arch/x86/mm/tlb.o
drivers/base/attribute_container.o
drivers/base/auxiliary.o
drivers/base/auxiliary_sysfs.o
drivers/base/bus.o
drivers/base/cacheinfo.o
drivers/base/class.o
drivers/base/component.o
drivers/base/container.o
drivers/base/core.o
drivers/base/cpu.o
drivers/base/dd.o
drivers/base/devres.o
drivers/base/devtmpfs.o
drivers/base/driver.o
drivers/base/faux.o
drivers/base/firmware.o
drivers/base/init.o
drivers/base/map.o
drivers/base/module.o
drivers/base/node.o
drivers/base/physical_location.o
drivers/base/platform-msi.o
drivers/base/platform.o
drivers/base/property.o
drivers/base/swnode.o
drivers/base/syscore.o
drivers/base/topology.o
drivers/base/trace.o
drivers/base/transport_class.o
fs/aio.o
fs/anon_inodes.o
fs/attr.o
fs/bad_inode.o
fs/binfmt_elf.o
fs/binfmt_misc.o
fs/binfmt_script.o
fs/buffer.o
fs/char_dev.o
fs/compat_binfmt_elf.o
fs/coredump.o
fs/dcache.o
fs/direct-io.o
fs/d_path.o
fs/drop_caches.o
fs/eventfd.o
fs/eventpoll.o
fs/exec.o
fs/fcntl.o
fs/fhandle.o
fs/file_attr.o
fs/file.o
fs/filesystems.o
fs/file_table.o
fs/fs_context.o
fs/fsopen.o
fs/fs_parser.o
fs/fs_pin.o
fs/fs_struct.o
fs/fs_types.o
fs/fs-writeback.o
fs/init.o
fs/inode.o
fs/ioctl.o
fs/kernel_read_file.o
fs/libfs.o
fs/locks.o
fs/mbcache.o
fs/mnt_idmapping.o
fs/mpage.o
fs/namei.o
fs/namespace.o
fs/nsfs.o
fs/open.o
fs/pidfs.o
fs/pipe.o
fs/pnode.o
fs/posix_acl.o
fs/proc_namespace.o
fs/readdir.o
fs/read_write.o
fs/remap_range.o
fs/select.o
fs/seq_file.o
fs/signalfd.o
fs/splice.o
fs/stack.o
fs/statfs.o
fs/stat.o
fs/super.o
fs/sync.o
fs/sysctls.o
fs/timerfd.o
fs/utimes.o
fs/xattr.o
init/calibrate.o
init/do_mounts_initrd.o
init/do_mounts.o
init/initramfs.o
init/init_task.o
init/main.o
init/version.o
init/version-timestamp.o
ipc/compat.o
ipc/ipc_sysctl.o
ipc/mq_sysctl.o
ipc/mqueue.o
ipc/msg.o
ipc/msgutil.o
ipc/namespace.o
ipc/sem.o
ipc/shm.o
ipc/syscall.o
ipc/util.o
kernel/acct.o
kernel/async.o
kernel/auditfilter.o
kernel/audit_fsnotify.o
kernel/audit.o
kernel/auditsc.o
kernel/audit_tree.o
kernel/audit_watch.o
kernel/capability.o
kernel/compat.o
kernel/context_tracking.o
kernel/cpu.o
kernel/crash_core.o
kernel/crash_reserve.o
kernel/cred.o
kernel/delayacct.o
kernel/dma.o
kernel/elfcorehdr.o
kernel/exec_domain.o
kernel/exit.o
kernel/extable.o
kernel/fork.o
kernel/freezer.o
kernel/groups.o
kernel/iomem.o
kernel/irq_work.o
kernel/jump_label.o
kernel/kallsyms.o
kernel/kcmp.o
kernel/kexec_core.o
kernel/kexec.o
kernel/kprobes.o
kernel/ksyms_common.o
kernel/ksysfs.o
kernel/kthread.o
kernel/notifier.o
kernel/nscommon.o
kernel/nsproxy.o
kernel/nstree.o
kernel/padata.o
kernel/panic.o
kernel/params.o
kernel/pid_namespace.o
kernel/pid.o
kernel/profile.o
kernel/ptrace.o
kernel/range.o
kernel/reboot.o
kernel/regset.o
kernel/relay.o
kernel/resource.o
kernel/rseq.o
kernel/seccomp.o
kernel/signal.o
kernel/smpboot.o
kernel/smp.o
kernel/softirq.o
kernel/stacktrace.o
kernel/static_call_inline.o
kernel/static_call.o
kernel/stop_machine.o
kernel/sysctl.o
kernel/sys_ni.o
kernel/sys.o
kernel/taskstats.o
kernel/task_work.o
kernel/tracepoint.o
kernel/tsacct.o
kernel/ucount.o
kernel/uid16.o
kernel/umh.o
kernel/user.o
kernel/utsname.o
kernel/utsname_sysctl.o
kernel/vmcore_info.o
kernel/workqueue.o
lib/argv_split.o
lib/asn1_decoder.o
lib/assoc_array.o
lib/base64.o
lib/bcd.o
lib/bitmap.o
lib/bitmap-str.o
lib/bitrev.o
lib/bsearch.o
lib/bucket_locks.o
lib/bug.o
lib/buildid.o
lib/bust_spinlocks.o
lib/check_signature.o
lib/clz_ctz.o
lib/clz_tab.o
lib/cmdline.o
lib/cpumask.o
lib/cpu_rmap.o
lib/ctype.o
lib/debug_locks.o
lib/dec_and_lock.o
lib/decompress_bunzip2.o
lib/decompress_inflate.o
lib/decompress.o
lib/decompress_unlz4.o
lib/decompress_unlzma.o
lib/decompress_unlzo.o
lib/decompress_unxz.o
lib/decompress_unzstd.o
lib/devres.o
lib/dump_stack.o
lib/dynamic_queue_limits.o
lib/earlycpio.o
lib/errname.o
lib/errseq.o
lib/extable.o
lib/find_bit.o
lib/flex_proportions.o
lib/fw_table.o
lib/genalloc.o
lib/generic-radix-tree.o
lib/glob.o
lib/group_cpus.o
lib/hexdump.o
lib/idr.o
lib/interval_tree.o
lib/iomap_copy.o
lib/iomap.o
lib/iomem_copy.o
lib/iov_iter.o
lib/irq_regs.o
lib/is_single_threaded.o
lib/kasprintf.o
lib/kfifo.o
lib/klist.o
lib/kobject.o
lib/kobject_uevent.o
lib/kstrtox.o
lib/list_sort.o
lib/llist.o
lib/lockref.o
lib/logic_pio.o
lib/lwq.o
lib/maple_tree.o
lib/memcat_p.o
lib/memweight.o
lib/net_utils.o
lib/nlattr.o
lib/nmi_backtrace.o
lib/objpool.o
lib/oid_registry.o
lib/once.o
lib/parser.o
lib/percpu_counter.o
lib/percpu-refcount.o
lib/plist.o
lib/radix-tree.o
lib/random32.o
lib/ratelimit.o
lib/rbtree.o
lib/rcuref.o
lib/refcount.o
lib/rhashtable.o
lib/sbitmap.o
lib/scatterlist.o
lib/seq_buf.o
lib/sg_pool.o
lib/siphash.o
lib/sort.o
lib/stackdepot.o
lib/string_helpers.o
lib/string.o
lib/strncpy_from_user.o
lib/strnlen_user.o
lib/syscall.o
lib/sys_info.o
lib/timerqueue.o
lib/ucs2_string.o
lib/union_find.o
lib/usercopy.o
lib/uuid.o
lib/vsprintf.o
lib/win_minmax.o
lib/xarray.o
lib/xxhash.o
mm/backing-dev.o
mm/compaction.o
mm/debug.o
mm/dmapool.o
mm/early_ioremap.o
mm/execmem.o
mm/fadvise.o
mm/filemap.o
mm/folio-compat.o
mm/gup.o
mm/highmem.o
mm/hugetlb_cgroup.o
mm/hugetlb.o
mm/hugetlb_vmemmap.o
mm/init-mm.o
mm/interval_tree.o
mm/list_lru.o
mm/maccess.o
mm/madvise.o
mm/memblock.o
mm/memfd.o
mm/memory.o
mm/memory-tiers.o
mm/mempolicy.o
mm/mempool.o
mm/migrate.o
mm/mincore.o
mm/mlock.o
mm/mmap_lock.o
mm/mmap.o
mm/mm_init.o
mm/mmu_gather.o
mm/mmu_notifier.o
mm/mmzone.o
mm/mprotect.o
mm/mremap.o
mm/mseal.o
mm/msync.o
mm/numa_memblks.o
mm/numa.o
mm/oom_kill.o
mm/page_alloc.o
mm/page_counter.o
mm/page_frag_cache.o
mm/page_io.o
mm/page_vma_mapped.o
mm/pagewalk.o
mm/page-writeback.o
mm/percpu.o
mm/pgtable-generic.o
mm/process_vm_access.o
mm/ptdump.o
mm/pt_reclaim.o
mm/readahead.o
mm/rmap.o
mm/secretmem.o
mm/shmem.o
mm/show_mem.o
mm/shrinker.o
mm/slab_common.o
mm/slub.o
mm/sparse.o
mm/sparse-vmemmap.o
mm/swapfile.o
mm/swap.o
mm/swap_state.o
mm/truncate.o
mm/util.o
mm/vma_exec.o
mm/vma_init.o
mm/vmalloc.o
mm/vma.o
mm/vmscan.o
mm/vmstat.o
mm/workingset.o
net/core/datagram.o
net/core/dev_addr_lists.o
net/core/dev_api.o
net/core/dev_ioctl.o
net/core/dev.o
net/core/dst_cache.o
net/core/dst.o
net/core/failover.o
net/core/fib_notifier.o
net/core/fib_rules.o
net/core/filter.o
net/core/flow_dissector.o
net/core/flow_offload.o
net/core/gen_estimator.o
net/core/gen_stats.o
net/core/gro_cells.o
net/core/gro.o
net/core/gso.o
net/core/hotdata.o
net/core/link_watch.o
net/core/neighbour.o
net/core/netclassid_cgroup.o
net/core/netdev-genl-gen.o
net/core/netdev-genl.o
net/core/netdev_queues.o
net/core/netdev_rx_queue.o
net/core/netevent.o
net/core/net_namespace.o
net/core/netpoll.o
net/core/netprio_cgroup.o
net/core/net-procfs.o
net/core/net-sysfs.o
net/core/net-traces.o
net/core/ptp_classifier.o
net/core/request_sock.o
net/core/rtnetlink.o
net/core/scm.o
net/core/secure_seq.o
net/core/selftests.o
net/core/skbuff.o
net/core/sock_diag.o
net/core/sock.o
net/core/sock_reuseport.o
net/core/stream.o
net/core/sysctl_net_core.o
net/core/tso.o
net/core/utils.o
net/core/xdp.o
<!-- corpus:end -->
```

## 3. aarch64 corpus (stage K7)

15 objects: the same core subsystems plus the two `arch/arm64` directories
that only exist on the second architecture. Stage K7 builds them with a GCC
16.2.0 cross compiler configured from this same source tree
(`--target=aarch64-linux-gnu --without-headers`) through the `crugc` launcher,
then checks that every result is an AArch64 relocatable object.

```
<!-- corpus-arm64:begin -->
arch/arm64/kernel/setup.o
arch/arm64/kernel/fpsimd.o
arch/arm64/mm/mmu.o
drivers/base/core.o
fs/open.o
fs/read_write.o
init/main.o
ipc/util.o
kernel/fork.o
kernel/sched/core.o
lib/string.o
lib/vsprintf.o
mm/page_alloc.o
mm/slub.o
net/core/skbuff.o
<!-- corpus-arm64:end -->
```

## 4. What the corpus does not claim

Compiling these objects is not a boot, a codegen-parity result or a conformance
class. Boot is stage K6; x86_64 codegen parity against the reference GCC is
Phase 21; aarch64 is object-level only, because stage K7 runs no aarch64 boot
(`KERNEL-BASELINE.md` section 5, phase-19 non-goals).
