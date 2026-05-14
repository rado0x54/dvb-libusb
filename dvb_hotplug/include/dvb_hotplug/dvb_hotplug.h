/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_hotplug — libusb hotplug surface for dvb_libusb consumers.
 *
 * Subscribes to libusb USB-device arrival and departure events,
 * filters them against the supported-board tables of every bridge
 * compiled into dvb_libusb (em28xx / dib0700 / dvbsky), and surfaces
 * the survivors as a queue of (kind, bridge, bus, devaddr, vid, pid)
 * descriptors. The consumer pops events on a wake-pipe write — the
 * standard select()/poll() integration pattern.
 *
 * Both ARRIVED and LEFT events are reported. Idle-device removals
 * (no active stream) are visible via LEFT — relying solely on URB
 * errors would miss them. Streaming consumers will still see -ENODEV
 * from `usbq_stream_read` on the same unplug; close paths need to be
 * idempotent.
 *
 * The hotplug module piggybacks on usbq's existing libusb event
 * thread; it does not spawn its own.
 */

#ifndef DVB_HOTPLUG_DVB_HOTPLUG_H
#define DVB_HOTPLUG_DVB_HOTPLUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dvb_hotplug_kind {
    DVB_HOTPLUG_ARRIVED = 1,
    DVB_HOTPLUG_LEFT    = 2,
} dvb_hotplug_kind_t;

typedef enum dvb_hotplug_bridge {
    DVB_HOTPLUG_BRIDGE_UNKNOWN = 0,
    DVB_HOTPLUG_BRIDGE_EM28XX,
    DVB_HOTPLUG_BRIDGE_DIB0700,
    DVB_HOTPLUG_BRIDGE_DVBSKY,
} dvb_hotplug_bridge_t;

typedef struct dvb_hotplug_event {
    dvb_hotplug_kind_t   kind;
    dvb_hotplug_bridge_t bridge;
    uint8_t              bus;          /* libusb_get_bus_number       */
    uint8_t              devaddr;      /* libusb_get_device_address   */
    uint16_t             vid;
    uint16_t             pid;
} dvb_hotplug_event_t;

/* Start delivering hotplug events. Registers a libusb hotplug
 * callback for ARRIVED|LEFT on every VID:PID listed by the three
 * `*_supported_boards()` tables. Each delivered event is queued
 * internally and signaled by writing a single zero byte to
 * `wake_fd` — the caller's non-blocking pipe write-end. The caller
 * select()s/poll()s on the matching read-end and drains the queue
 * with dvb_hotplug_pop().
 *
 * `wake_fd` must be >= 0 and writable; the module retains it for
 * the lifetime of the registration. Returns 0 on success, libusb-
 * style negative on failure. Calling twice without an intervening
 * dvb_hotplug_shutdown() returns -EBUSY.
 *
 * Initial-state behavior: libusb fires one ARRIVED callback per
 * already-plugged-in matching device at registration time, so the
 * caller's queue will have the current device set ready to drain
 * on the first read. */
int  dvb_hotplug_init(int wake_fd);

/* Pop one queued event. Returns 1 and writes to *out if an event
 * was available; 0 if the queue is empty; negative if hotplug is
 * not initialized. Loop until 0 after each wake — multiple events
 * between two wakes coalesce into a single readable byte on the
 * wake pipe. */
int  dvb_hotplug_pop(dvb_hotplug_event_t *out);

/* Stop delivering events. Deregisters the libusb hotplug callback,
 * drops usbq's event-thread refcount, drains the queue. Idempotent. */
void dvb_hotplug_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* DVB_HOTPLUG_DVB_HOTPLUG_H */
