/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userland port of drivers/media/usb/dvb-usb/dib0700_core.c (DiBcom
 * DiB0700 USB bridge). Same shape as em28xx/ — open/close on top of
 * a usbq device, USB control transfers for register R/W and GPIO,
 * an i²c-over-USB master, firmware upload, and a streaming-control
 * call that flips the bulk-IN endpoint on/off.
 *
 * License: GPL-2.0-or-later. The protocol is upstream's; this file
 * is a clean re-expression in our codebase shape (similar to em28xx/).
 *
 * Why not lift the upstream dib0700_core.c verbatim? It's tangled
 * with the dvb-usb framework (struct dvb_usb_device, the dvb-core
 * registration path, the kernel's URB infrastructure, the input
 * subsystem for IR), all of which we explicitly do not mirror in
 * userland. The chip drivers (mn88472/, tda18250/) sitting
 * downstream ARE lifted verbatim via linuxdvbkpi/.
 *
 * Authors of the upstream driver this port is derived from:
 *   DiBcom, SA — original protocol implementation
 *   Patrick Boettcher and others
 *
 * The xbox-one device-attach sequence in src/dib0700_xbox.c is a
 * direct re-expression of dib0700_devices.c::xbox_one_attach, with
 * the same GPIO power/reset timing and the same i²c addresses
 * (mn88472 @ 0x18, tda18250 @ 0x60). Per-board attach functions for
 * other dib0700 cards would each get their own _<card>.c file in
 * src/.
 */

#ifndef DIB0700_DIB0700_H
#define DIB0700_DIB0700_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usbq/usbq.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DiB07x0 GPIO names (subset; see upstream dib07x0.h for the full
 * mapping). Numeric values match the bridge's GPIO bit assignment. */
enum dib0700_gpio {
    DIB0700_GPIO0  = 0,
    DIB0700_GPIO1  = 2,
    DIB0700_GPIO2  = 3,
    DIB0700_GPIO3  = 4,
    DIB0700_GPIO4  = 5,
    DIB0700_GPIO5  = 6,
    DIB0700_GPIO6  = 8,
    DIB0700_GPIO7  = 10,
    DIB0700_GPIO8  = 11,
    DIB0700_GPIO9  = 14,
    DIB0700_GPIO10 = 15,
};

#define DIB0700_GPIO_IN   0
#define DIB0700_GPIO_OUT  1

/* USB bulk-IN endpoint for TS data on the xbox-one card (per upstream
 * dib0700_devices.c DIB0700_DEFAULT_STREAMING_CONFIG(0x82)). Other
 * cards may use 0x83 for a second adapter. */
#define DIB0700_TS_BULK_EP_XBOX  0x82

typedef struct dib0700_dev dib0700_dev_t;

/* Open the bridge over an already-claimed usbq device. Caller must
 * have done usbq_open + claim_interface(0). The handle borrows the
 * usbq pointer; caller still owns it.
 *
 * Returns NULL on alloc failure. The bridge state is not touched
 * until the first dib0700_is_cold() call — open is a pure allocation. */
dib0700_dev_t *dib0700_open(usbq_dev_t *usb);

/* Close the bridge handle. Does NOT close the usbq device. */
void dib0700_close(dib0700_dev_t *dev);

/* Issue REQUEST_GET_VERSION. Returns 1 if the chip is "cold" (no
 * firmware running — vendor requests fail or return zero bytes), 0
 * if "warm" (firmware running and reachable), negative errno on
 * USB error.
 *
 * On a freshly-plugged xbox tuner the bridge is cold: download
 * firmware via dib0700_download_firmware, sleep ~500ms for the chip
 * to jump into ramcode, then re-call this — it should now report
 * warm. */
int dib0700_is_cold(dib0700_dev_t *dev);

/* Get the running firmware version word. Only valid after firmware
 * has been downloaded successfully. Returns 0 on success; on failure
 * leaves *fw_version untouched. */
int dib0700_get_firmware_version(dib0700_dev_t *dev, uint32_t *fw_version);

/* Download the dib0700 ramcode (`dvb-usb-dib0700-1.20.fw`). Path
 * argument is the on-disk location of the blob; format is the
 * dib0700-flavoured Intel HEX records the upstream firmware ships
 * (each record: len byte + 2-byte addr + 1-byte type + payload + 1-byte
 * checksum). After upload, the chip jumps to 0x70000000 and the
 * vendor-request surface lights up. */
int dib0700_download_firmware(dib0700_dev_t *dev, const char *fw_path);

/* Set GPIO direction + level. */
int dib0700_set_gpio(dib0700_dev_t *dev, enum dib0700_gpio gpio,
                     int direction, int value);

/* Set the i²c bus clock speed. Default 100 kHz; common chips run
 * fine at 100 or 400 kHz. Range: 30 < scl_kHz < 1000. */
int dib0700_set_i2c_speed(dib0700_dev_t *dev, uint16_t scl_kHz);

/* Streaming ctrl: enables/disables the bulk-IN endpoint feeding the
 * adapter slot `adapter_idx` (0 or 1; xbox uses 0). The `disable_master`
 * flag mirrors the upstream `disable_streaming_master_mode = 1`
 * setting that the xbox board uses. */
int dib0700_streaming_ctrl(dib0700_dev_t *dev, int adapter_idx,
                           int onoff, int disable_master);

/* Returns the underlying usbq device handle. Borrowed pointer —
 * the dib0700 layer doesn't own the USB device, the caller does. */
usbq_dev_t *dib0700_usb_handle(dib0700_dev_t *dev);

/* ---- linuxdvbkpi i²c-adapter shim ----
 *
 * Returns a `struct i2c_adapter *` whose master_xfer routes through
 * the dib0700 i²c-over-USB protocol. Pass this to
 * `i2c_new_client_device(adap, &board_info)` to attach a lifted chip
 * driver (mn88472, tda18250, …).
 *
 * Pointer is owned by the dib0700_dev_t and stays valid until close.
 * It returns `void *` to keep the public header free of <linux/i2c.h>;
 * cast to `struct i2c_adapter *` at the call site. */
void *dib0700_get_i2c_adapter(dib0700_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* DIB0700_DIB0700_H */
