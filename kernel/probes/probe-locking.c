/* Phase 19 stage K1 header probe: spinlocks, mutexes, atomics and RCU.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/seqlock.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/lockdep.h>
#include <linux/local_lock.h>

static int __init probe_init (void)
{
	return 0;
}

static void __exit probe_exit (void)
{
}

module_init (probe_init);
module_exit (probe_exit);

MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("crug stage K1 header probe: spinlocks, mutexes, atomics and RCU");
