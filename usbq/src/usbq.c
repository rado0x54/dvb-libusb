/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * usbq — libusb wrapper. Single-file implementation: the device
 * surface (open/claim/control/bulk_read/bulk_write) plus the async
 * streaming layer (URB pool + ring buffer + wake_fd) all live here
 * because there's only one backend and the URB completion handler
 * pushes directly into the ring (no dispatcher indirection).
 *
 * The libusb event-handling thread is a singleton, refcounted by the
 * number of open async streams. First stream_open spawns it; last
 * stream_close joins it. Sync transfers don't need it (libusb drives
 * its own events on the calling thread for those).
 */

#include "usbq/usbq.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libusb.h>

/* ---- library lifecycle ------------------------------------------- *
 *
 * usbq_init / usbq_shutdown are refcounted because multiple DVB
 * engines (em28xx, dib0700, …) each call them independently as part
 * of their own discover/shutdown lifecycle. Without the refcount,
 * the first engine to tear down would call libusb_exit() while
 * other engines still hold open device handles — libusb's darwin
 * backend then logs "device still referenced at libusb_exit" and
 * asserts inside its pthread_mutex_destroy() pass.
 *
 * Mutex around the counter so multi-threaded consumers (engine
 * threads, status pollers) don't race on the 0↔1 transitions that
 * call into libusb_init/exit. */

static libusb_context *g_ctx;
static int             g_init_refcount;
static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;

int usbq_init(void) {
    pthread_mutex_lock(&g_init_lock);
    int rc = 0;
    if (g_init_refcount == 0) {
        rc = libusb_init(&g_ctx);
        if (rc != 0) {
            pthread_mutex_unlock(&g_init_lock);
            return rc;
        }
    }
    g_init_refcount++;
    pthread_mutex_unlock(&g_init_lock);
    return 0;
}

void usbq_shutdown(void) {
    pthread_mutex_lock(&g_init_lock);
    if (g_init_refcount == 0) {
        pthread_mutex_unlock(&g_init_lock);
        return;
    }
    if (--g_init_refcount == 0 && g_ctx) {
        libusb_exit(g_ctx);
        g_ctx = NULL;
    }
    pthread_mutex_unlock(&g_init_lock);
}

/* ---- per-device state -------------------------------------------- */

struct usbq_dev {
    libusb_device_handle           *handle;
    libusb_device                  *device;
    struct libusb_device_descriptor desc;
    int                             auto_detach;
};

static int parse_vidpid(const char *path, uint16_t *vid_out, uint16_t *pid_out) {
    unsigned vid = 0, pid = 0;
    char extra = 0;
    if (!path) return -1;
    int n = sscanf(path, "%x:%x%c", &vid, &pid, &extra);
    if ((n != 2 && !(n == 3 && extra == '\0')) ||
        vid > 0xFFFF || pid > 0xFFFF) {
        return -1;
    }
    *vid_out = (uint16_t)vid;
    *pid_out = (uint16_t)pid;
    return 0;
}

usbq_dev_t *usbq_open(const char *vidpid) {
    uint16_t vid, pid;
    if (parse_vidpid(vidpid, &vid, &pid) != 0) return NULL;
    if (usbq_init() != 0) return NULL;

    libusb_device_handle *h =
        libusb_open_device_with_vid_pid(g_ctx, vid, pid);
    if (!h) return NULL;

    usbq_dev_t *d = calloc(1, sizeof(*d));
    if (!d) { libusb_close(h); return NULL; }
    d->handle = h;
    d->device = libusb_get_device(h);
    if (libusb_get_device_descriptor(d->device, &d->desc) != 0) {
        libusb_close(h);
        free(d);
        return NULL;
    }
    int rc = libusb_set_auto_detach_kernel_driver(h, 1);
    d->auto_detach = (rc == 0);
    return d;
}

void usbq_close(usbq_dev_t *d) {
    if (!d) return;
    if (d->handle) libusb_close(d->handle);
    free(d);
}

int usbq_reset(usbq_dev_t *d) {
    return d ? libusb_reset_device(d->handle) : -EINVAL;
}

int usbq_enumerate(const char *vidpid, usbq_device_info_t *out, int max) {
    if (!out || max <= 0) return -EINVAL;
    uint16_t want_vid = 0, want_pid = 0;
    int filter = 0;
    if (vidpid) {
        if (parse_vidpid(vidpid, &want_vid, &want_pid) != 0) return -EINVAL;
        filter = 1;
    }
    if (usbq_init() != 0) return -EIO;

    libusb_device **list;
    ssize_t n = libusb_get_device_list(g_ctx, &list);
    if (n < 0) return (int)n;

    int written = 0;
    for (ssize_t i = 0; i < n && written < max; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (filter && (desc.idVendor != want_vid || desc.idProduct != want_pid))
            continue;
        snprintf(out[written].vidpid, sizeof(out[written].vidpid),
                 "%04x:%04x", desc.idVendor, desc.idProduct);
        out[written].bus_number     = libusb_get_bus_number(list[i]);
        out[written].device_address = libusb_get_device_address(list[i]);
        written++;
    }
    libusb_free_device_list(list, 1);
    return written;
}

/* ---- descriptor probes ------------------------------------------- */

int usbq_get_device_descriptor(usbq_dev_t *d, void *out, size_t len) {
    if (!d) return -EINVAL;
    return libusb_control_transfer(d->handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD |
        LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR,
        (uint16_t)(LIBUSB_DT_DEVICE << 8) | 0,
        0, out, (uint16_t)len, 1000);
}

int usbq_get_configspace(usbq_dev_t *d, void *out, size_t len) {
    if (!d) return -EINVAL;
    return libusb_control_transfer(d->handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD |
        LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR,
        (uint16_t)(LIBUSB_DT_CONFIG << 8) | 0,
        0, out, (uint16_t)len, 1000);
}

int usbq_get_string_descriptor_ascii(usbq_dev_t *d, uint8_t index,
                                     char *out, size_t len) {
    if (!d) return -EINVAL;
    if (len == 0) return LIBUSB_ERROR_INVALID_PARAM;
    if (index == 0) { out[0] = '\0'; return 0; }
    int rc = libusb_get_string_descriptor_ascii(
        d->handle, index, (unsigned char *)out, (int)len);
    if (rc < 0) out[0] = '\0';
    return rc;
}

/* ---- interface management ---------------------------------------- */

int usbq_claim_interface(usbq_dev_t *d, int iface) {
    return d ? libusb_claim_interface(d->handle, iface) : -EINVAL;
}

int usbq_release_interface(usbq_dev_t *d, int iface) {
    return d ? libusb_release_interface(d->handle, iface) : -EINVAL;
}

int usbq_set_interface(usbq_dev_t *d, int iface, int alt) {
    return d ? libusb_set_interface_alt_setting(d->handle, iface, alt)
             : -EINVAL;
}

int usbq_disconnect_kernel_driver(usbq_dev_t *d) {
    if (!d) return -EINVAL;
    if (d->auto_detach) return 0;
    int rc = libusb_kernel_driver_active(d->handle, 0);
    if (rc == 1) {
        return libusb_detach_kernel_driver(d->handle, 0);
    }
    return 0;
}

/* ---- sync transfers ---------------------------------------------- */

int usbq_control(usbq_dev_t *d,
                 uint8_t bmRequestType, uint8_t bRequest,
                 uint16_t wValue, uint16_t wIndex,
                 void *buf, uint16_t wLength,
                 uint32_t timeout_ms) {
    if (!d) return -EINVAL;
    return libusb_control_transfer(d->handle,
        bmRequestType, bRequest, wValue, wIndex,
        (unsigned char *)buf, wLength, timeout_ms);
}

int usbq_bulk_read(usbq_dev_t *d, uint8_t ep,
                   void *buf, size_t len, uint32_t timeout_ms) {
    if (!d) return -EINVAL;
    if (len > INT32_MAX) return LIBUSB_ERROR_INVALID_PARAM;
    int actual = 0;
    int rc = libusb_bulk_transfer(d->handle, ep,
        (unsigned char *)buf, (int)len, &actual, timeout_ms);
    if (rc < 0 && rc != LIBUSB_ERROR_TIMEOUT) return rc;
    return actual;
}

int usbq_bulk_write(usbq_dev_t *d, uint8_t ep,
                    const void *buf, size_t len, uint32_t timeout_ms) {
    if (!d) return -EINVAL;
    if (len > INT32_MAX) return LIBUSB_ERROR_INVALID_PARAM;
    int actual = 0;
    int rc = libusb_bulk_transfer(d->handle, ep,
        (unsigned char *)(uintptr_t)buf, (int)len, &actual, timeout_ms);
    if (rc < 0 && rc != LIBUSB_ERROR_TIMEOUT) return rc;
    return actual;
}

int usbq_request_access(usbq_dev_t *d) {
    (void)d;
    return 0;
}

/* ---- async streaming: singleton event thread + URB pool ---------- */

static struct {
    pthread_mutex_t mu;
    pthread_t       tid;
    int             refcount;
    int             alive;
    int             stop;
} g_evt = { .mu = PTHREAD_MUTEX_INITIALIZER };

static void *evt_thread_main(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&g_evt.stop, __ATOMIC_ACQUIRE)) {
        struct timeval tv = { 0, 100 * 1000 };
        (void)libusb_handle_events_timeout_completed(g_ctx, &tv, NULL);
    }
    return NULL;
}

static int evt_acquire(void) {
    pthread_mutex_lock(&g_evt.mu);
    if (g_evt.refcount++ == 0) {
        __atomic_store_n(&g_evt.stop, 0, __ATOMIC_RELEASE);
        int rc = pthread_create(&g_evt.tid, NULL, evt_thread_main, NULL);
        if (rc != 0) {
            g_evt.refcount = 0;
            pthread_mutex_unlock(&g_evt.mu);
            return LIBUSB_ERROR_OTHER;
        }
        g_evt.alive = 1;
    }
    pthread_mutex_unlock(&g_evt.mu);
    return 0;
}

static void evt_release(void) {
    pthread_mutex_lock(&g_evt.mu);
    if (g_evt.refcount > 0 && --g_evt.refcount == 0 && g_evt.alive) {
        __atomic_store_n(&g_evt.stop, 1, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_evt.mu);
        pthread_join(g_evt.tid, NULL);
        pthread_mutex_lock(&g_evt.mu);
        g_evt.alive = 0;
    }
    pthread_mutex_unlock(&g_evt.mu);
}

typedef struct urb_slot {
    struct libusb_transfer *xfer;
    uint8_t                *buf;
    usbq_stream_t          *stream;
    uint8_t                 state;       /* 0 normal, 1 cancel-pending */
} urb_slot_t;

struct usbq_stream {
    /* libusb URB pool */
    urb_slot_t      *urbs;
    uint32_t         pool_depth;
    int              pending_cancel;
    int              stopping;

    /* ring buffer + reader API */
    pthread_mutex_t  mu;
    pthread_cond_t   cv;
    uint8_t         *ring;
    size_t           ring_cap;
    size_t           ring_head;
    size_t           ring_tail;
    size_t           ring_used;
    uint64_t         overflow_bytes;
    int              wake_fd;        /* -1 = disabled */
};

static void ring_push_locked(usbq_stream_t *s, const uint8_t *src, size_t len) {
    if (len == 0) return;
    if (len > s->ring_cap) {
        s->overflow_bytes += (len - s->ring_cap);
        src += (len - s->ring_cap);
        len  = s->ring_cap;
    }
    if (s->ring_used + len > s->ring_cap) {
        size_t drop = (s->ring_used + len) - s->ring_cap;
        if (drop > s->ring_used) drop = s->ring_used;
        s->ring_head = (s->ring_head + drop) % s->ring_cap;
        s->ring_used -= drop;
        s->overflow_bytes += drop;
    }
    size_t first = s->ring_cap - s->ring_tail;
    if (first > len) first = len;
    memcpy(s->ring + s->ring_tail, src, first);
    if (first < len) {
        memcpy(s->ring, src + first, len - first);
    }
    s->ring_tail = (s->ring_tail + len) % s->ring_cap;
    s->ring_used += len;
}

static void LIBUSB_CALL stream_xfer_cb(struct libusb_transfer *xfer) {
    urb_slot_t    *u = xfer->user_data;
    usbq_stream_t *s = u->stream;

    if (xfer->status == LIBUSB_TRANSFER_CANCELLED) {
        pthread_mutex_lock(&s->mu);
        if (--s->pending_cancel <= 0) {
            s->pending_cancel = 0;
            pthread_cond_broadcast(&s->cv);
        }
        pthread_mutex_unlock(&s->mu);
        return;
    }

    /* Success path: push bytes into the ring + signal readers + wake fd. */
    if ((xfer->status == LIBUSB_TRANSFER_COMPLETED ||
         xfer->status == LIBUSB_TRANSFER_TIMED_OUT) &&
        xfer->actual_length > 0 && !s->stopping) {
        pthread_mutex_lock(&s->mu);
        ring_push_locked(s, u->buf, (size_t)xfer->actual_length);
        pthread_cond_signal(&s->cv);
        int wake_fd = s->wake_fd;
        pthread_mutex_unlock(&s->mu);
        if (wake_fd >= 0) {
            const char zero = 0;
            ssize_t w = write(wake_fd, &zero, 1);
            (void)w;
        }
    }

    pthread_mutex_lock(&s->mu);
    int stopping = s->stopping;
    int my_state = u->state;
    pthread_mutex_unlock(&s->mu);

    if (stopping || my_state != 0) return;

    int rc = libusb_submit_transfer(xfer);
    if (rc < 0) {
        /* Resubmit failure (typically NO_DEVICE after unplug) — flip
         * stopping so the rest of the pool doesn't try to keep going.
         * The URB is now terminal; close()/restart() won't get a
         * cancellation callback for it (LIBUSB_ERROR_NOT_FOUND). */
        pthread_mutex_lock(&s->mu);
        s->stopping = 1;
        pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mu);
    }
}

usbq_stream_t *usbq_stream_open(usbq_dev_t *dev, const usbq_stream_cfg_t *cfg) {
    if (!dev || !cfg || (cfg->endpoint & 0x80) == 0) {
        return NULL;
    }
    uint32_t buf_size = cfg->buf_size      ? cfg->buf_size      : USBQ_STREAM_DEFAULT_BUF_SIZE;
    uint32_t depth    = cfg->pool_depth    ? cfg->pool_depth    : USBQ_STREAM_DEFAULT_POOL_DEPTH;
    uint32_t ring_cap = cfg->ring_capacity ? cfg->ring_capacity : USBQ_STREAM_DEFAULT_RING_BYTES;

    usbq_stream_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->pool_depth = depth;
    s->ring_cap   = ring_cap;
    s->wake_fd    = cfg->wake_fd > 0 ? cfg->wake_fd : -1;

    s->ring = malloc(ring_cap);
    if (!s->ring) goto fail;

    s->urbs = calloc(depth, sizeof(*s->urbs));
    if (!s->urbs) goto fail;
    for (uint32_t i = 0; i < depth; i++) {
        s->urbs[i].buf    = malloc(buf_size);
        s->urbs[i].xfer   = libusb_alloc_transfer(0);
        s->urbs[i].stream = s;
        if (!s->urbs[i].buf || !s->urbs[i].xfer) goto fail;
    }
    if (evt_acquire() != 0) goto fail;

    for (uint32_t i = 0; i < depth; i++) {
        libusb_fill_bulk_transfer(s->urbs[i].xfer,
            dev->handle, cfg->endpoint,
            s->urbs[i].buf, (int)buf_size,
            stream_xfer_cb, &s->urbs[i],
            /*timeout=*/0);
        int rc = libusb_submit_transfer(s->urbs[i].xfer);
        if (rc < 0) {
            pthread_mutex_lock(&s->mu);
            s->stopping = 1;
            for (uint32_t j = 0; j < i; j++) {
                if (s->urbs[j].state == 0) {
                    s->urbs[j].state = 1;
                    int crc = libusb_cancel_transfer(s->urbs[j].xfer);
                    if (crc == 0) s->pending_cancel++;
                }
            }
            while (s->pending_cancel > 0) {
                pthread_cond_wait(&s->cv, &s->mu);
            }
            pthread_mutex_unlock(&s->mu);
            evt_release();
            goto fail;
        }
    }
    return s;

fail:
    if (s->urbs) {
        for (uint32_t i = 0; i < depth; i++) {
            if (s->urbs[i].xfer) libusb_free_transfer(s->urbs[i].xfer);
            free(s->urbs[i].buf);
        }
        free(s->urbs);
    }
    free(s->ring);
    pthread_mutex_destroy(&s->mu);
    pthread_cond_destroy(&s->cv);
    free(s);
    return NULL;
}

void usbq_stream_close(usbq_stream_t *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mu);
    s->stopping = 1;
    /* Trust libusb_cancel_transfer's return value: only count a
     * cancel as pending if it actually queued one. After a hot
     * unplug some URBs are already terminal (NOT_FOUND); waiting on
     * a callback that won't fire would block SIGINT shutdown forever. */
    for (uint32_t i = 0; i < s->pool_depth; i++) {
        if (s->urbs[i].state == 0) {
            s->urbs[i].state = 1;
            int rc = libusb_cancel_transfer(s->urbs[i].xfer);
            if (rc == 0) s->pending_cancel++;
        }
    }
    while (s->pending_cancel > 0) {
        pthread_cond_wait(&s->cv, &s->mu);
    }
    pthread_mutex_unlock(&s->mu);

    for (uint32_t i = 0; i < s->pool_depth; i++) {
        libusb_free_transfer(s->urbs[i].xfer);
        free(s->urbs[i].buf);
    }
    free(s->urbs);
    free(s->ring);
    pthread_mutex_destroy(&s->mu);
    pthread_cond_destroy(&s->cv);
    free(s);

    evt_release();
}

int usbq_stream_read(usbq_stream_t *s, void *buf, size_t cap, uint32_t timeout_ms) {
    if (!s || !buf || cap == 0) return -EINVAL;
    pthread_mutex_lock(&s->mu);
    if (s->ring_used == 0 && timeout_ms > 0 && !s->stopping) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        while (s->ring_used == 0 && !s->stopping) {
            int rc = pthread_cond_timedwait(&s->cv, &s->mu, &deadline);
            if (rc != 0) break;
        }
    }
    size_t want = cap;
    if (want > s->ring_used) want = s->ring_used;
    if (want == 0) {
        pthread_mutex_unlock(&s->mu);
        return 0;
    }
    size_t first = s->ring_cap - s->ring_head;
    if (first > want) first = want;
    memcpy(buf, s->ring + s->ring_head, first);
    if (first < want) {
        memcpy((uint8_t *)buf + first, s->ring, want - first);
    }
    s->ring_head = (s->ring_head + want) % s->ring_cap;
    s->ring_used -= want;
    pthread_mutex_unlock(&s->mu);
    return (int)want;
}

void usbq_stream_flush(usbq_stream_t *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mu);
    s->ring_head = s->ring_tail = s->ring_used = 0;
    pthread_mutex_unlock(&s->mu);
}

int usbq_stream_restart(usbq_stream_t *s) {
    if (!s) return -EINVAL;

    pthread_mutex_lock(&s->mu);
    if (s->stopping) {
        pthread_mutex_unlock(&s->mu);
        return -EINVAL;
    }
    /* Cancel every still-submitted URB. NOT_FOUND from
     * libusb_cancel_transfer means the URB is terminal — no callback
     * will fire, so don't add to pending_cancel. */
    for (uint32_t i = 0; i < s->pool_depth; i++) {
        if (s->urbs[i].state == 0) {
            s->urbs[i].state = 1;
            int rc = libusb_cancel_transfer(s->urbs[i].xfer);
            if (rc == 0) s->pending_cancel++;
        }
    }
    while (s->pending_cancel > 0) {
        pthread_cond_wait(&s->cv, &s->mu);
    }
    /* Reset cancel marks so the on-completion callback resubmits. */
    for (uint32_t i = 0; i < s->pool_depth; i++) {
        s->urbs[i].state = 0;
    }
    pthread_mutex_unlock(&s->mu);

    /* Resubmit each transfer. xfer/buf/endpoint are unchanged from
     * the original libusb_fill_bulk_transfer at open time. */
    for (uint32_t i = 0; i < s->pool_depth; i++) {
        int rc = libusb_submit_transfer(s->urbs[i].xfer);
        if (rc < 0) {
            pthread_mutex_lock(&s->mu);
            s->stopping = 1;
            pthread_cond_broadcast(&s->cv);
            pthread_mutex_unlock(&s->mu);
            return rc;
        }
    }
    return 0;
}

uint64_t usbq_stream_overflow_bytes(const usbq_stream_t *s) {
    if (!s) return 0;
    pthread_mutex_lock((pthread_mutex_t *)&s->mu);
    uint64_t v = s->overflow_bytes;
    pthread_mutex_unlock((pthread_mutex_t *)&s->mu);
    return v;
}
