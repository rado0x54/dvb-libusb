/* SPDX-License-Identifier: MIT */
/*
 * linuxdvbkpi — Linux kernel API polyfill for userland chip-driver lift.
 *
 * Goal: compile upstream Linux DVB demod / tuner sources from
 * drivers/media/dvb-frontends and drivers/media/tuners UNCHANGED in
 * userland by providing the kernel-API surface they consume —
 * i2c_client, regmap, dvb_frontend, mutex, kmalloc, request_firmware,
 * dev_*, etc.
 *
 * Scope:
 *
 *   This polyfill targets the *narrow* subset of kernel APIs that the
 *   demod/tuner drivers actually use. It is NOT a generic kernel-API
 *   shim. USB urb infrastructure, dvb-core registration, kfifo, the
 *   input subsystem — all explicitly out of scope. Bridge drivers
 *   (em28xx, dib0700, …) tangle into those subsystems and are therefore
 *   manually ported in this repo rather than lifted verbatim.
 *
 *   What's covered:
 *
 *     - <linux/types.h>            u8/u16/u32/u64, __be16/__be32, bool
 *     - <linux/errno.h>            -EINVAL, -EIO, -ENODEV, …
 *     - <linux/i2c.h>              i2c_client, i2c_msg, i2c_adapter,
 *                                  i2c_transfer, i2c_master_send/recv,
 *                                  i2c_get/set_clientdata,
 *                                  i2c_new_dummy_device, i2c_board_info,
 *                                  i2c_new_client_device, i2c_driver,
 *                                  module_i2c_driver
 *     - <linux/regmap.h>           regmap_init_i2c, devm_regmap_init_i2c,
 *                                  regmap_read/write/bulk_read/
 *                                  bulk_write/write_bits/exit
 *     - <linux/firmware.h>         struct firmware,
 *                                  request_firmware, release_firmware,
 *                                  MODULE_FIRMWARE
 *     - <linux/mutex.h>            mutex, mutex_init, mutex_lock/unlock
 *     - <linux/delay.h>            msleep, usleep_range
 *     - <linux/jiffies.h>          jiffies, msecs_to_jiffies, …
 *     - <linux/slab.h>             kzalloc, kzalloc_obj, kfree
 *     - <linux/err.h>              IS_ERR, PTR_ERR, ERR_PTR
 *     - <linux/kernel.h>           ARRAY_SIZE, container_of, BIT,
 *                                  min/max, DIV_ROUND_*, MHz, …
 *     - <linux/dev_printk.h>       dev_dbg, dev_info, dev_err
 *     - <linux/int_log.h>          intlog10
 *     - <linux/module.h>           MODULE_*, EXPORT_SYMBOL_GPL,
 *                                  module_i2c_driver
 *     - <media/dvb_frontend.h>     dvb_frontend, dvb_frontend_ops,
 *                                  dvb_tuner_ops, dtv_frontend_properties,
 *                                  enum fe_status, FE_HAS_*, FE_SCALE_*,
 *                                  FE_CAN_*, NO_STREAM_ID_FILTER
 *
 *   What's NOT covered:
 *
 *     - struct urb / usb_submit_urb / kernel USB IO — bridge drivers
 *       implement bulk streaming via usbq_stream_t directly.
 *     - dvb_register_adapter / dmxdev / kfifo — consumers of this
 *       library bring their own upper layer; we do not mirror /dev/dvb.
 *     - rc_dev / input subsystem — IR remote-control input is not
 *       wired through chip drivers in our stack.
 *
 * Driver-registration model in userland:
 *
 *   Upstream's `module_i2c_driver(foo_driver)` registers an i2c_driver
 *   with the kernel i2c-core; the core calls `foo_driver.probe(client)`
 *   when an i2c_new_client_device matches. We replicate that with a
 *   process-local registry: the macro becomes a constructor that
 *   appends the driver to a static linked list, and our shim's
 *   `i2c_new_client_device(adap, info)` looks up by `info->type`,
 *   allocates an i2c_client, and calls the matched probe.
 *
 *   Upper-layer code that wants a chip attached calls:
 *
 *       struct i2c_client *c = i2c_new_client_device(adapter, &info);
 *       struct dvb_frontend *fe = config.get_dvb_frontend(c);
 *
 *   exactly as the kernel side would — and the upstream chip driver
 *   stays unchanged.
 */

#ifndef LINUXDVBKPI_LINUXDVBKPI_H
#define LINUXDVBKPI_LINUXDVBKPI_H

/* The umbrella header — pulled in by the chip-driver source via
 * `-include linuxdvbkpi/linuxdvbkpi.h` in meson, so the upstream
 * `#include <media/dvb_frontend.h>` etc. resolve to OUR headers
 * under linuxdvbkpi/include/.
 *
 * We pre-include the kernel-API headers most chip drivers transitively
 * pull in (kernel.h for DIV_ROUND_*, slab.h for kzalloc, module.h for
 * MODULE_*, etc.). Upstream Linux gets these via a long chain rooted
 * at <linux/sched.h> + <linux/i2c.h>; we short-circuit. */

#include <linuxdvbkpi/firmware_root.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/kconfig.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/workqueue.h>
#include <linux/regmap.h>
#include <linux/firmware.h>
#include <linux/int_log.h>
#include <media/dvb_frontend.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set the directory `request_firmware()` should look in. The lookup
 * order is:
 *   1. The directory passed to linuxdvbkpi_set_firmware_root().
 *   2. The directory pointed to by the env var $FIRMWARE_DIR, if set.
 *   3. A small list of common system paths (/usr/local/lib/firmware,
 *      /usr/lib/firmware, /lib/firmware).
 *
 * Pass NULL to clear an explicit setting. Thread-safety: caller is
 * responsible for not racing this with concurrent request_firmware
 * calls — typically called once at process startup. */
void linuxdvbkpi_set_firmware_root(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* LINUXDVBKPI_LINUXDVBKPI_H */
