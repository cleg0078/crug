/* Phase 19 stage K1 header probe: sockets, sk_buff and net devices.
 *
 * Compiled as an external module against the pinned kernel tree. The body is
 * the minimum a module needs; the include list is the test.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <net/net_namespace.h>
#include <linux/etherdevice.h>

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
MODULE_DESCRIPTION ("crug stage K1 header probe: sockets, sk_buff and net devices");
