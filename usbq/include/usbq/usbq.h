/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * usbq — thin libusb wrapper. Owns the USB-device handle, sync
 * control/bulk transfers, and an async streaming layer on top of a
 * pool-of-N submitted bulk-IN URBs feeding an internal ring buffer.
 *
 * Designed to keep the chip-driver layers (linuxdvbkpi consumers,
 * em28xx / dib0700 bridges, engine_em28xx / engine_dib0700) ignorant
 * of libusb. They see only `usbq_*` symbols.
 *
 * License: GPL-2.0-or-later (combined work; the chip drivers and
 * bridge ports we link with are GPL-2.0+).
 */

#ifndef USBQ_USBQ_H
#define USBQ_USBQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usbq_dev    usbq_dev_t;
typedef struct usbq_stream usbq_stream_t;

/* ---- Library lifecycle (idempotent; refcount-free) ---------------- */

int  usbq_init(void);
void usbq_shutdown(void);

/* Access usbq's singleton libusb_context. Returns NULL before
 * usbq_init() and after the final usbq_shutdown(). Intended for the
 * hotplug module (or any consumer that needs to register libusb
 * callbacks on the same context the streams + open calls run on).
 *
 * Forward-declared so this header doesn't pull in <libusb.h>; the
 * caller includes that itself. */
struct libusb_context;
struct libusb_context *usbq_libusb_context(void);

/* Bump / drop the refcount on usbq's singleton libusb-events thread.
 * That thread normally lives only while at least one usbq_stream_t
 * is open. Components that need libusb callbacks delivered when no
 * stream is open (notably the hotplug module — a plugged-but-idle
 * device produces no URB activity) call usbq_evt_acquire to keep
 * the thread alive, and usbq_evt_release on teardown.
 *
 * Returns 0 on success, libusb-style negative on thread-create failure. */
int  usbq_evt_acquire(void);
void usbq_evt_release(void);

/* ---- Per-device API ---------------------------------------------- *
 *
 * `vidpid` is "VVVV:PPPP" hex (e.g. "2040:0265"). usbq_open returns
 * the first matching device, or NULL. Caller closes via usbq_close. */

usbq_dev_t *usbq_open  (const char *vidpid);
void        usbq_close (usbq_dev_t *dev);
int         usbq_reset (usbq_dev_t *dev);

/* Open the specific device at USB bus_number:device_address (as
 * reported by libusb_get_bus_number / libusb_get_device_address).
 * Differs from usbq_open by VID:PID — that returns the first
 * matching device, which is the wrong physical device when two
 * boards with the same VID:PID are plugged in. Hotplug callbacks
 * provide bus:devaddr for exactly this disambiguation.
 * Returns NULL if no device is at the given address, or if open
 * fails. Caller should subsequently consult usbq_get_vidpid() to
 * decide whether the device matches its supported-board table. */
usbq_dev_t *usbq_open_by_addr(uint8_t bus_number, uint8_t device_address);

/* Read the cached USB VID:PID of an opened device, captured at
 * usbq_open / usbq_open_by_addr time. Useful after open-by-address,
 * when the caller looks up its board record by VID:PID. */
int         usbq_get_vidpid(usbq_dev_t *dev, uint16_t *vid_out,
                            uint16_t *pid_out);

/* Enumerate plugged-in USB devices matching `vidpid` ("VVVV:PPPP"
 * hex) without opening any of them. Does not claim, does not bring
 * up; just walks the bus. Useful for "what hardware is here?"
 * checks before any firmware path is configured. Pass NULL for
 * vidpid to list every USB device on the host.
 *
 * Writes up to `max` entries to `out`. Returns the number written,
 * or a negative errno on error. The returned bus_number +
 * device_address pair uniquely identifies one physical device on
 * the host within the current bus state. */
typedef struct usbq_device_info {
    char    vidpid[16];        /* "VVVV:PPPP" — null-terminated */
    uint8_t bus_number;
    uint8_t device_address;
} usbq_device_info_t;

int         usbq_enumerate (const char *vidpid,
                            usbq_device_info_t *out, int max);

int usbq_get_device_descriptor       (usbq_dev_t *dev, void *out, size_t len);
int usbq_get_configspace             (usbq_dev_t *dev, void *out, size_t len);
int usbq_get_string_descriptor_ascii (usbq_dev_t *dev, uint8_t index,
                                      char *out, size_t len);

int usbq_claim_interface          (usbq_dev_t *dev, int iface);
int usbq_release_interface        (usbq_dev_t *dev, int iface);
int usbq_set_interface            (usbq_dev_t *dev, int iface, int alt);
int usbq_disconnect_kernel_driver (usbq_dev_t *dev);

/* Sync transfers. Return libusb-style: bytes-on-success, negative on
 * error. Timeouts are milliseconds. */

int usbq_control(usbq_dev_t *dev,
                 uint8_t bmRequestType, uint8_t bRequest,
                 uint16_t wValue, uint16_t wIndex,
                 void *buf, uint16_t wLength,
                 uint32_t timeout_ms);

int usbq_bulk_read(usbq_dev_t *dev, uint8_t ep,
                   void *buf, size_t len,
                   uint32_t timeout_ms);

/* usbq_bulk_write: `ep` must have bit 7 CLEAR (OUT endpoint). Returns
 * bytes actually written; libusb-style negative on error. Used by chip
 * firmware uploaders that ship records via bulk-OUT (the dib0700
 * bridge's REQUEST_JUMPRAM + intel-hex flow). */
int usbq_bulk_write(usbq_dev_t *dev, uint8_t ep,
                    const void *buf, size_t len,
                    uint32_t timeout_ms);

/* macOS USB-access permission stub. No-op on Linux/FreeBSD; reserved
 * for first-time-claim Apple-permission paths if we ever need them. */
int usbq_request_access(usbq_dev_t *dev);

/* ---- Async streaming ---------------------------------------------- *
 *
 * usbq keeps `pool_depth` submitted bulk-IN URBs of `buf_size` bytes
 * permanently in flight. Each completion pushes bytes into an internal
 * ring (`ring_capacity` bytes, overwrite-oldest on overflow) and
 * resubmits. Consumers call usbq_stream_read for blocking dequeue. */

#define USBQ_STREAM_DEFAULT_BUF_SIZE       48128u  /* matches em28xx upstream */
#define USBQ_STREAM_DEFAULT_POOL_DEPTH        16u
#define USBQ_STREAM_DEFAULT_RING_BYTES   (8u * 1024u * 1024u)

typedef struct usbq_stream_cfg {
    uint8_t  endpoint;       /* bulk-IN endpoint with bit 7 set, e.g. 0x84 */
    uint32_t buf_size;       /* per-URB buffer (bytes); 0 = default       */
    uint32_t pool_depth;     /* URBs in flight; 0 = default               */
    uint32_t ring_capacity;  /* ring bytes; 0 = default                   */
    /* If > 0, usbq writes a single zero byte to this fd whenever the
     * completion callback pushes new data into the ring. Intended for
     * select()/poll() integration: caller owns a non-blocking pipe,
     * passes the write-end here, and select()s on the read-end to know
     * when usbq_stream_read will return bytes. 0 (the zero-init
     * default) disables wake signaling — fd 0 is stdin and never
     * a wake fd in practice. */
    int      wake_fd;
} usbq_stream_cfg_t;

usbq_stream_t *usbq_stream_open(usbq_dev_t *dev, const usbq_stream_cfg_t *cfg);
void           usbq_stream_close(usbq_stream_t *s);

/* Blocking read up to `cap` bytes from the stream's ring. Returns
 * bytes copied; 0 on timeout / transient gap; negative on hard error.
 * In particular, returns -ENODEV when the stream is stopping (device
 * unplugged or submission failure) and the ring has been fully
 * drained — that's the signal callers use to tear down a dead device,
 * versus 0 which can still be transient. Pass timeout_ms = 0 for
 * non-blocking. */
int            usbq_stream_read(usbq_stream_t *s, void *buf, size_t cap,
                                uint32_t timeout_ms);

/* Drop everything currently in the ring without touching the URB pool.
 * Intended for retune flows where stale bytes from the previous source
 * shouldn't bleed into the new one. */
void           usbq_stream_flush(usbq_stream_t *s);

/* Cancel every URB currently submitted to libusb, wait for the
 * cancellations to complete, then resubmit fresh URBs on the same
 * endpoint. Mirrors upstream Linux dvb-usb-v2's "kill+resubmit URBs
 * around streaming-control toggle" pattern, which some USB bridges
 * (notably DiB0700) need to recover their endpoint state machine
 * across enable/disable cycles.
 *
 * Caller is responsible for calling this around the bridge-specific
 * streaming-enable/disable sequence. The ring buffer is left intact
 * — call usbq_stream_flush separately if pre-restart bytes should
 * be dropped. Returns 0 on success, negative errno on failure. */
int            usbq_stream_restart(usbq_stream_t *s);

uint64_t       usbq_stream_overflow_bytes(const usbq_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* USBQ_USBQ_H */
