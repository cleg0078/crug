/* Phase 19 stage K1 header probe: devices, platform devices and firmware description.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>
#include <linux/device/class.h>
#include <linux/property.h>

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
MODULE_DESCRIPTION ("crug stage K1 header probe: devices, platform devices and firmware description");
