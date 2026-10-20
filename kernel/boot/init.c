/* Phase 19 stage K6 boot smoke init.
 *
 * This is the userspace half of the boot check: a freestanding program with no
 * libc, no Crug runtime and no shared objects. It writes one marker line to the
 * serial console and asks the kernel to power off. The marker proves that the
 * kernel reached userspace and executed code compiled by crugc; the poweroff
 * proves the syscall path works and lets qemu exit instead of hanging.
 *
 * Built by scripts/kernel-stage.sh K6:
 *   crugc -O2 -static -nostdlib -fno-pie -no-pie -o init init.c
 *
 * x86_64 only, like the rest of stage K6 (KERNEL-BASELINE.md section 5).
 */

typedef unsigned long u64;

static long
sys1 (long n, long a)
{
  long ret;
  __asm__ volatile ("syscall"
                    : "=a" (ret)
                    : "a" (n), "D" (a)
                    : "rcx", "r11", "memory");
  return ret;
}

static long
sys3 (long n, long a, long b, long c)
{
  long ret;
  __asm__ volatile ("syscall"
                    : "=a" (ret)
                    : "a" (n), "D" (a), "S" (b), "d" (c)
                    : "rcx", "r11", "memory");
  return ret;
}

/* The fourth argument goes in r10, not rcx: the syscall instruction clobbers
   rcx and r11. */
static long
sys4 (long n, long a, long b, long c, long d)
{
  register long r10 __asm__ ("r10") = d;
  long ret;
  __asm__ volatile ("syscall"
                    : "=a" (ret)
                    : "a" (n), "D" (a), "S" (b), "d" (c), "r" (r10)
                    : "rcx", "r11", "memory");
  return ret;
}

#define SYS_write 1
#define SYS_reboot 169
#define SYS_exit_group 231

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

__attribute__ ((noreturn)) void
_start (void)
{
  static const char marker[] = "crug-boot-ok\n";

  sys3 (SYS_write, 1, (long) marker, (long) (sizeof marker - 1));
  sys4 (SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
        LINUX_REBOOT_CMD_POWER_OFF, 0);
  sys1 (SYS_exit_group, 0);
  for (;;)
    ;
}
