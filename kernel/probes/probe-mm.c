/* Phase 19 stage K1 header probe: memory management.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mmzone.h>
#include <linux/page-flags.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

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
MODULE_DESCRIPTION ("crug stage K1 header probe: memory management");
