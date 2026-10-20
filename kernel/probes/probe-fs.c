/* Phase 19 stage K1 header probe: VFS, page cache and name resolution.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/mount.h>
#include <linux/fs_types.h>
#include <linux/writeback.h>

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
MODULE_DESCRIPTION ("crug stage K1 header probe: VFS, page cache and name resolution");
