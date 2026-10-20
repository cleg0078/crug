/* Phase 19 stage K1 header probe: scheduler, wait queues and kthreads.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched/mm.h>
#include <linux/sched/debug.h>

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
MODULE_DESCRIPTION ("crug stage K1 header probe: scheduler, wait queues and kthreads");
