/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_hotplug — libusb hotplug arrival/departure delivery to a
 * consumer-supplied wake fd. Filters MATCH_ANY hotplug events
 * against the three bridges' supported-board tables resolved
 * dynamically (so adding a new board automatically participates).
 *
 * Threading model:
 *   - We do NOT spawn an event thread. usbq already runs one and
 *     refcounts it; we bump that refcount via usbq_evt_acquire
 *     so callbacks keep firing even when no stream is open.
 *   - The libusb callback runs on usbq's event thread. It enqueues
 *     under g_mu and writes one byte to wake_fd. No further work
 *     happens on that thread — open() / bring-up is the consumer's
 *     responsibility (it usually wants to do that on a worker
 *     thread since firmware upload takes seconds).
 */

#include "dvb_hotplug/dvb_hotplug.h"

#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_dib0700/dvb_dib0700.h"
#include "dvb_dvbsky/dvb_dvbsky.h"
#include "dvb_handle/dvb_handle.h"
#include "usbq/usbq.h"

#include <libusb.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HLOG(...)                                                              \
    do {                                                                       \
        fprintf(stderr, "[dvb_hotplug] " __VA_ARGS__);                         \
        fprintf(stderr, "\n");                                                 \
    } while (0)

/* Bounded ring of queued events. 64 is plenty — between two wake
 * pipe reads the queue should rarely exceed the device count on
 * the host. Overflow drops oldest. */
#define HP_QUEUE_CAP 64

typedef struct hp_state {
    pthread_mutex_t                  mu;
    int                              initialized;
    int                              wake_fd;     /* -1 if not registered */
    int                              acquired_evt;
    libusb_hotplug_callback_handle   cb_handle;
    dvb_hotplug_event_t              queue[HP_QUEUE_CAP];
    uint32_t                         q_head;
    uint32_t                         q_tail;
    uint32_t                         q_used;
} hp_state_t;

static hp_state_t g_hp = { .mu = PTHREAD_MUTEX_INITIALIZER, .wake_fd = -1 };

/* ---- VID:PID lookup --------------------------------------------- */

static int vidpid_parse(const char *s, uint16_t *vid, uint16_t *pid) {
    unsigned v = 0, p = 0;
    if (sscanf(s, "%x:%x", &v, &p) != 2 || v > 0xFFFFu || p > 0xFFFFu)
        return -1;
    *vid = (uint16_t)v;
    *pid = (uint16_t)p;
    return 0;
}

/* Resolve a VID:PID to its bridge by walking the three supported-
 * board tables. Returns DVB_HOTPLUG_BRIDGE_UNKNOWN if no match —
 * the caller drops the event. */
static dvb_hotplug_bridge_t bridge_for_vidpid(uint16_t vid, uint16_t pid) {
    static const struct {
        const dvb_supported_board_t *(*get)(int *);
        dvb_hotplug_bridge_t          tag;
    } tables[] = {
        { dvb_em28xx_supported_boards,  DVB_HOTPLUG_BRIDGE_EM28XX  },
        { dvb_dib0700_supported_boards, DVB_HOTPLUG_BRIDGE_DIB0700 },
        { dvb_dvbsky_supported_boards,  DVB_HOTPLUG_BRIDGE_DVBSKY  },
    };
    for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        int n = 0;
        const dvb_supported_board_t *b = tables[t].get(&n);
        for (int i = 0; i < n; i++) {
            for (int j = 0; b[i].vidpids && b[i].vidpids[j]; j++) {
                uint16_t bv, bp;
                if (vidpid_parse(b[i].vidpids[j], &bv, &bp) != 0) continue;
                if (bv == vid && bp == pid) return tables[t].tag;
            }
        }
    }
    return DVB_HOTPLUG_BRIDGE_UNKNOWN;
}

/* ---- Queue + wake ------------------------------------------------ */

static void enqueue_and_wake(const dvb_hotplug_event_t *ev) {
    pthread_mutex_lock(&g_hp.mu);
    if (g_hp.q_used == HP_QUEUE_CAP) {
        /* Drop oldest; a consumer that's this far behind is
         * already going to re-scan via *_scan_present(). */
        g_hp.q_head = (g_hp.q_head + 1) % HP_QUEUE_CAP;
        g_hp.q_used--;
    }
    g_hp.queue[g_hp.q_tail] = *ev;
    g_hp.q_tail = (g_hp.q_tail + 1) % HP_QUEUE_CAP;
    g_hp.q_used++;
    int fd = g_hp.wake_fd;
    pthread_mutex_unlock(&g_hp.mu);

    if (fd >= 0) {
        const char zero = 0;
        ssize_t w = write(fd, &zero, 1);
        (void)w;
    }
}

/* ---- libusb callback -------------------------------------------- */

static int LIBUSB_CALL hotplug_cb(libusb_context *ctx, libusb_device *dev,
                                  libusb_hotplug_event event, void *user_data) {
    (void)ctx;
    (void)user_data;

    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != 0) return 0;

    dvb_hotplug_bridge_t bridge =
        bridge_for_vidpid(desc.idVendor, desc.idProduct);
    if (bridge == DVB_HOTPLUG_BRIDGE_UNKNOWN) {
        /* Not one of ours — drop. With MATCH_ANY filtering on the
         * libusb side, this is where the actual filter lives. */
        return 0;
    }

    dvb_hotplug_event_t ev = {
        .kind    = (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
                   ? DVB_HOTPLUG_ARRIVED : DVB_HOTPLUG_LEFT,
        .bridge  = bridge,
        .bus     = libusb_get_bus_number(dev),
        .devaddr = libusb_get_device_address(dev),
        .vid     = desc.idVendor,
        .pid     = desc.idProduct,
    };
    enqueue_and_wake(&ev);
    return 0;
}

/* ---- Public API -------------------------------------------------- */

int dvb_hotplug_init(int wake_fd) {
    if (wake_fd < 0) return -EINVAL;

    pthread_mutex_lock(&g_hp.mu);
    if (g_hp.initialized) {
        pthread_mutex_unlock(&g_hp.mu);
        return -EBUSY;
    }
    /* Reset queue from any prior cycle. */
    g_hp.q_head = g_hp.q_tail = g_hp.q_used = 0;
    g_hp.wake_fd = wake_fd;
    pthread_mutex_unlock(&g_hp.mu);

    if (usbq_init() != 0) return -EIO;

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        HLOG("libusb build lacks hotplug capability — feature unavailable");
        usbq_shutdown();
        pthread_mutex_lock(&g_hp.mu);
        g_hp.wake_fd = -1;
        pthread_mutex_unlock(&g_hp.mu);
        return -ENOTSUP;
    }

    /* Keep usbq's event thread alive even with no streams open;
     * otherwise our hotplug callback never gets dispatched. */
    int rc = usbq_evt_acquire();
    if (rc != 0) {
        HLOG("usbq_evt_acquire failed: %d", rc);
        usbq_shutdown();
        pthread_mutex_lock(&g_hp.mu);
        g_hp.wake_fd = -1;
        pthread_mutex_unlock(&g_hp.mu);
        return rc;
    }

    /* MATCH_ANY VID/PID — the bridge-table filter inside hotplug_cb
     * does the real selection. ENUMERATE makes libusb fire one
     * ARRIVED callback per already-plugged-in matching device, so
     * consumers see a coherent initial state. */
    rc = libusb_hotplug_register_callback(
        usbq_libusb_context(),
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_ENUMERATE,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        hotplug_cb, NULL, &g_hp.cb_handle);
    if (rc != LIBUSB_SUCCESS) {
        HLOG("libusb_hotplug_register_callback failed: %d", rc);
        usbq_evt_release();
        usbq_shutdown();
        pthread_mutex_lock(&g_hp.mu);
        g_hp.wake_fd = -1;
        pthread_mutex_unlock(&g_hp.mu);
        return rc;
    }

    pthread_mutex_lock(&g_hp.mu);
    g_hp.initialized  = 1;
    g_hp.acquired_evt = 1;
    pthread_mutex_unlock(&g_hp.mu);
    return 0;
}

int dvb_hotplug_pop(dvb_hotplug_event_t *out) {
    if (!out) return -EINVAL;
    pthread_mutex_lock(&g_hp.mu);
    if (!g_hp.initialized) {
        pthread_mutex_unlock(&g_hp.mu);
        return -EINVAL;
    }
    if (g_hp.q_used == 0) {
        pthread_mutex_unlock(&g_hp.mu);
        return 0;
    }
    *out = g_hp.queue[g_hp.q_head];
    g_hp.q_head = (g_hp.q_head + 1) % HP_QUEUE_CAP;
    g_hp.q_used--;
    pthread_mutex_unlock(&g_hp.mu);
    return 1;
}

void dvb_hotplug_shutdown(void) {
    pthread_mutex_lock(&g_hp.mu);
    if (!g_hp.initialized) {
        pthread_mutex_unlock(&g_hp.mu);
        return;
    }
    libusb_hotplug_callback_handle cb = g_hp.cb_handle;
    int acquired = g_hp.acquired_evt;
    g_hp.initialized  = 0;
    g_hp.acquired_evt = 0;
    g_hp.wake_fd      = -1;
    g_hp.q_head = g_hp.q_tail = g_hp.q_used = 0;
    pthread_mutex_unlock(&g_hp.mu);

    libusb_hotplug_deregister_callback(usbq_libusb_context(), cb);
    if (acquired) usbq_evt_release();
    usbq_shutdown();
}
