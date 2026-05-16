/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_em28xx — bridge lifecycle for em28xx-based DVB USB devices.
 *
 * Open path (one device):
 *   usbq_open(vidpid) → claim → em28xx_open
 *     → board.gpio_seq + i2c_speed + xclk
 *     → for each frontend: board.attach(parent_adap, fn) which i2c_new_client_devices
 *       the demod + tuner via the linuxdvbkpi registry
 *     → fe->ops.init  /  fe->ops.tuner_ops.init
 *     → usbq_stream_open on the frontend's bulk-IN endpoint
 *     → publish a dvb_frontend_handle_t into *handles[]
 *
 * The engine is board-agnostic — chip-specific knowledge lives in
 * boards.c attach functions. Adding a new board = adding one row +
 * one attach fn there; this file doesn't change.
 *
 * The vtable here delegates per-frontend operations through
 * `dvb_em28xx_frontend_t *` instances, which carry the open
 * dvb_frontend + i2c_clients + usbq_stream + wake pipe.
 */

#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_em28xx_priv.h"

#include "dvb_handle/dvb_debug.h"
#include "em28xx/em28xx.h"
#include "usbq/usbq.h"
#include <linuxdvbkpi/linuxdvbkpi.h>
#include <linux/dvb/frontend.h>
#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ELOG(...)                                                              \
    do {                                                                       \
        fprintf(stderr, "[dvb_em28xx] " __VA_ARGS__);                       \
        fprintf(stderr, "\n");                                                 \
    } while (0)

/* ---- Per-frontend state. Owned by the parent device. ------------- */

typedef struct dvb_em28xx_frontend {
    struct i2c_client    *demod_client;     /* owned by linuxdvbkpi i2c */
    struct dvb_frontend  *fe;
    struct i2c_client    *tuner_client;
    int                   demod_init_done;
    int                   tuner_init_done;
    int                   capture_on;
    int                   ts_index;          /* same as fe index for em28xx */
    uint8_t               ts_endpoint;
    uint32_t              tune_count;        /* incremented on each tune */

    usbq_stream_t        *stream;
    int                   wake_pipe[2];      /* [0]=read, [1]=write; -1 if unset */

    /* Back-pointer to the parent device for capture toggle (we need
     * the em28xx bridge for em28xx_capture_start). */
    struct dvb_em28xx_dev *dev;

    /* Public handle published to callers. Populated at open. */
    dvb_frontend_handle_t handle;
} dvb_em28xx_frontend_t;

typedef struct dvb_em28xx_dev {
    usbq_dev_t              *usb;            /* owned */
    em28xx_dev_t            *bridge;         /* owned */
    pthread_mutex_t          bridge_lock;
    const em28xx_board_t    *board;
    dvb_em28xx_frontend_t frontends[4];   /* upper bound on fan-out */
    int                      frontend_count;

    struct dvb_em28xx_dev *next;          /* registry chain */
} dvb_em28xx_dev_t;

static dvb_em28xx_dev_t *g_devices = NULL;
static int                  g_usbq_inited = 0;

/* ---- Vtable thunks ----------------------------------------------- *
 *
 * `state` is `dvb_em28xx_frontend_t *`. */

static int em28xx_v_tune(void *state, const dvb_tune_params_t *p) {
    dvb_em28xx_frontend_t *fe = state;
    if (!fe || !fe->fe || !p) return -EINVAL;

    fe->tune_count++;
    DVBDBG("em28xx fn=%d tune#%u entry: delsys=%u freq=%u bw=%u sr=%u stream_id=%d "
           "(prev capture_on=%d, prev overflow=%llu)",
           fe->ts_index, fe->tune_count, p->delsys, p->freq_hz,
           p->bandwidth_hz, p->symbol_rate, p->stream_id, fe->capture_on,
           (unsigned long long)
           (fe->stream ? usbq_stream_overflow_bytes(fe->stream) : 0));

    /* Populate dtv_property_cache — chip drivers read this in
     * set_frontend / tuner_ops.set_params. */
    struct dtv_frontend_properties *c = &fe->fe->dtv_property_cache;
    c->delivery_system = (enum fe_delivery_system)p->delsys;
    c->frequency       = p->freq_hz;
    c->bandwidth_hz    = p->bandwidth_hz ? p->bandwidth_hz : 8000000u;
    c->symbol_rate     = p->symbol_rate;
    c->stream_id       = (p->stream_id < 0)
                         ? NO_STREAM_ID_FILTER
                         : (u32)p->stream_id;
    c->modulation      = QAM_AUTO;
    c->inversion       = INVERSION_AUTO;

    if (!fe->fe->ops.set_frontend) return -ENOSYS;
    int rc = fe->fe->ops.set_frontend(fe->fe);
    DVBDBG("em28xx fn=%d tune#%u set_frontend rc=%d", fe->ts_index, fe->tune_count, rc);
    if (rc < 0) return rc;

    /* Drop stale bytes before flipping em28xx capture on. */
    usbq_stream_flush(fe->stream);

    if (!fe->capture_on) {
        rc = em28xx_capture_start(fe->dev->bridge, fe->ts_index, /*enable=*/1);
        DVBDBG("em28xx fn=%d capture_start(1) rc=%d", fe->ts_index, rc);
        if (rc < 0) return rc;
        fe->capture_on = 1;
    } else {
        DVBDBG("em28xx fn=%d capture already on (no toggle)", fe->ts_index);
    }
    return 0;
}

static int em28xx_v_read_ts(void *state, void *buf, size_t len,
                            uint32_t timeout_ms) {
    dvb_em28xx_frontend_t *fe = state;
    if (!fe || !buf || len == 0) return -EINVAL;
    if (!fe->stream) return 0;
    return usbq_stream_read(fe->stream, buf, len, timeout_ms);
}

static int em28xx_v_get_status(void *state, dvb_status_t *out) {
    dvb_em28xx_frontend_t *fe = state;
    if (!fe || !out || !fe->fe || !fe->fe->ops.read_status) {
        if (out) memset(out, 0, sizeof(*out));
        return -EINVAL;
    }
    enum fe_status st = 0;
    int rc = fe->fe->ops.read_status(fe->fe, &st);
    if (rc < 0) return rc;
    out->has_signal   = !!(st & FE_HAS_SIGNAL);
    out->has_carrier  = !!(st & FE_HAS_CARRIER);
    out->has_viterbi  = !!(st & FE_HAS_VITERBI);
    out->has_sync     = !!(st & FE_HAS_SYNC);
    out->has_lock     = !!(st & FE_HAS_LOCK);

    /* Chip drivers (si2168, mn88472, lgdt3306a) write CNR into
     * dtv_property_cache.cnr.stat[0] in dB × 1000 when locked. */
    out->cnr_db_x1000 = 0;
    struct dtv_frontend_properties *c = &fe->fe->dtv_property_cache;
    if (c->cnr.len > 0 && c->cnr.stat[0].scale == FE_SCALE_DECIBEL) {
        out->cnr_db_x1000 = (int32_t)c->cnr.stat[0].svalue;
    }
    DVBDBG("em28xx fn=%d status: 0x%02x (%c%c%c%c%c) cnr=%d.%03d dB",
           fe->ts_index, (unsigned)st,
           (st & FE_HAS_SIGNAL)  ? 'S' : '-',
           (st & FE_HAS_CARRIER) ? 'C' : '-',
           (st & FE_HAS_VITERBI) ? 'V' : '-',
           (st & FE_HAS_SYNC)    ? 'Y' : '-',
           (st & FE_HAS_LOCK)    ? 'L' : '-',
           out->cnr_db_x1000 / 1000, abs(out->cnr_db_x1000) % 1000);
    return 0;
}

static int em28xx_v_event_fd(void *state) {
    dvb_em28xx_frontend_t *fe = state;
    return fe ? fe->wake_pipe[0] : -1;
}

static int em28xx_v_capture_stop(void *state) {
    dvb_em28xx_frontend_t *fe = state;
    if (!fe) return -EINVAL;
    if (!fe->capture_on) {
        DVBDBG("em28xx fn=%d capture_stop: already off", fe->ts_index);
        return 0;
    }
    int rc = em28xx_capture_start(fe->dev->bridge, fe->ts_index, /*enable=*/0);
    DVBDBG("em28xx fn=%d capture_start(0) rc=%d (overflow_bytes=%llu)",
           fe->ts_index, rc,
           (unsigned long long)
           (fe->stream ? usbq_stream_overflow_bytes(fe->stream) : 0));
    if (rc < 0) return rc;
    fe->capture_on = 0;
    return 0;
}

/* Per-frontend close. The vtable's close is a no-op — the engine
 * tears everything down through dvb_em28xx_shutdown(). */
static void em28xx_v_close(void *state) {
    (void)state;
}

static const dvb_engine_vtable_t k_em28xx_vtable = {
    em28xx_v_tune,
    em28xx_v_read_ts,
    em28xx_v_get_status,
    em28xx_v_event_fd,
    em28xx_v_capture_stop,
    em28xx_v_close,
};

/* ---- Bring-up helpers -------------------------------------------- */

static const em28xx_board_t *find_board_by_vidpid(const char *vidpid) {
    if (!vidpid) return NULL;
    for (const em28xx_board_t *b = em28xx_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i]; i++) {
            if (strcmp(b->vidpids[i], vidpid) == 0) return b;
        }
    }
    return NULL;
}

static int frontend_open(dvb_em28xx_dev_t *dev, int fn,
                         struct i2c_adapter *parent_adap) {
    dvb_em28xx_frontend_t *fe = &dev->frontends[fn];
    fe->dev          = dev;
    fe->ts_index     = fn;
    fe->ts_endpoint  = dev->board->ts_endpoints[fn];
    fe->wake_pipe[0] = -1;
    fe->wake_pipe[1] = -1;

    if (pipe(fe->wake_pipe) < 0) return -errno;
    fcntl(fe->wake_pipe[0], F_SETFL,
          fcntl(fe->wake_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(fe->wake_pipe[1], F_SETFL,
          fcntl(fe->wake_pipe[1], F_GETFL) | O_NONBLOCK);

    /* Board-specific chip attach. After this call the demod's
     * dvb_frontend has its tuner_ops populated by the tuner's
     * probe() (memcpy via si2157::probe etc.). */
    int rc = dev->board->attach(parent_adap, fn,
                                &fe->demod_client, &fe->fe,
                                &fe->tuner_client);
    if (rc < 0) {
        ELOG("%s: board attach(fn=%d) failed: %d", dev->board->name, fn, rc);
        return rc;
    }

    /* fe->ops.init does demod firmware upload (where applicable);
     * tuner_ops.init powers up the tuner. */
    if (fe->fe->ops.init) {
        rc = fe->fe->ops.init(fe->fe);
        if (rc < 0) {
            ELOG("%s: demod init(fn=%d) failed: %d",
                 dev->board->name, fn, rc);
            return rc;
        }
        fe->demod_init_done = 1;
    }
    if (fe->fe->ops.tuner_ops.init) {
        rc = fe->fe->ops.tuner_ops.init(fe->fe);
        if (rc < 0) {
            ELOG("%s: tuner init(fn=%d) failed: %d",
                 dev->board->name, fn, rc);
            return rc;
        }
        fe->tuner_init_done = 1;
    }

    usbq_stream_cfg_t scfg = {
        .endpoint = fe->ts_endpoint,
        .wake_fd  = fe->wake_pipe[1],
    };
    fe->stream = usbq_stream_open(dev->usb, &scfg);
    if (!fe->stream) {
        ELOG("%s: usbq_stream_open(ep=0x%02x) failed",
             dev->board->name, fe->ts_endpoint);
        return -EIO;
    }

    /* Publish handle. */
    char name_buf[96];
    if (dev->board->num_frontends > 1) {
        snprintf(name_buf, sizeof(name_buf), "%s (fe%d)",
                 dev->board->name, fn);
    } else {
        snprintf(name_buf, sizeof(name_buf), "%s", dev->board->name);
    }
    /* The handle's display_name is a `const char *` whose lifetime
     * must outlive every consumer. We allocate per-frontend so it
     * survives shutdown call ordering. */
    fe->handle.display_name = strdup(name_buf);

    fe->handle.ops          = &k_em28xx_vtable;
    fe->handle.engine_state = fe;
    fe->handle.bridge_lock  = &dev->bridge_lock;
    fe->handle.supported_delsys       = dev->board->supported_delsys;
    fe->handle.supported_delsys_count = dev->board->supported_delsys_count;
    (void)usbq_get_bus_devaddr(dev->usb,
                               &fe->handle.bus_number,
                               &fe->handle.device_address);
    return 0;
}

static void frontend_teardown(dvb_em28xx_frontend_t *fe) {
    if (!fe) return;
    if (fe->capture_on && fe->dev && fe->dev->bridge) {
        em28xx_capture_start(fe->dev->bridge, fe->ts_index, /*enable=*/0);
        fe->capture_on = 0;
    }
    if (fe->stream) {
        usbq_stream_close(fe->stream);
        fe->stream = NULL;
    }
    if (fe->fe) {
        if (fe->tuner_init_done && fe->fe->ops.tuner_ops.sleep) {
            (void)fe->fe->ops.tuner_ops.sleep(fe->fe);
        }
        if (fe->demod_init_done && fe->fe->ops.sleep) {
            (void)fe->fe->ops.sleep(fe->fe);
        }
    }
    /* Tuner before demod — unregistering the demod first would tear
     * down the i2c-mux the tuner is registered on. */
    if (fe->tuner_client) i2c_unregister_device(fe->tuner_client);
    if (fe->demod_client) i2c_unregister_device(fe->demod_client);
    if (fe->wake_pipe[1] >= 0) { close(fe->wake_pipe[1]); fe->wake_pipe[1] = -1; }
    if (fe->wake_pipe[0] >= 0) { close(fe->wake_pipe[0]); fe->wake_pipe[0] = -1; }
    free((void *)fe->handle.display_name);
    fe->handle.display_name = NULL;
}

/* Take ownership of an already-opened, kernel-driver-detached,
 * interface-0-claimed usbq_dev_t, bring up the board's frontends,
 * and chain onto g_devices. Returns the number of frontends
 * published, or 0 on any bring-up failure (releases/closes the
 * passed-in `usb` and cleans up everything else it allocated). */
static int open_device_from_usb(const em28xx_board_t *board, usbq_dev_t *usb,
                                dvb_frontend_handle_t **out_handles, int max_out) {
    if (board->num_frontends > (int)(sizeof(((dvb_em28xx_dev_t *)0)->frontends)
                                     / sizeof(((dvb_em28xx_dev_t *)0)->frontends[0]))) {
        ELOG("%s: num_frontends=%d exceeds engine capacity",
             board->name, board->num_frontends);
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }
    if (max_out < board->num_frontends) {
        ELOG("%s: caller has %d slot(s), need %d — skipping",
             board->name, max_out, board->num_frontends);
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }

    dvb_em28xx_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }
    pthread_mutex_init(&dev->bridge_lock, NULL);
    dev->usb   = usb;
    dev->board = board;

    dev->bridge = em28xx_open(usb);
    if (!dev->bridge ||
        em28xx_chip_id(dev->bridge) != EM28XX_CHIP_ID_EM28174) {
        ELOG("%s: em28174 chip-id check failed", board->name);
        goto fail;
    }
    if (em28xx_i2c_set_speed(dev->bridge, board->i2c_speed) < 0)  goto fail;
    if (em28xx_set_xclk     (dev->bridge, board->xclk)      < 0)  goto fail;
    if (em28xx_gpio_set     (dev->bridge, EM28XX_MODE_DIGITAL,
                             board->gpio_seq) < 0)               goto fail;

    /* Dual-TS boards (WinTV-dualHD): wire EP5 to TS2 packetizer. With-
     * out this, TS1 still streams but TS2 emits a frozen filler word. */
    if (board->num_frontends > 1) {
        if (em28xx_enable_dual_ts_bulk(dev->bridge) < 0) {
            ELOG("%s: dual-TS enable (R0B sequence) failed", board->name);
            goto fail;
        }
    }

    struct i2c_adapter *parent_adap = em28xx_get_i2c_adapter(dev->bridge, 1);
    if (!parent_adap) goto fail;

    for (int fn = 0; fn < board->num_frontends; fn++) {
        if (frontend_open(dev, fn, parent_adap) < 0) {
            for (int j = 0; j < fn; j++) frontend_teardown(&dev->frontends[j]);
            goto fail;
        }
        dev->frontend_count = fn + 1;
    }

    /* Publish handles. */
    for (int fn = 0; fn < board->num_frontends; fn++) {
        out_handles[fn] = &dev->frontends[fn].handle;
    }

    /* Chain onto the registry. */
    dev->next  = g_devices;
    g_devices  = dev;

    ELOG("%s: %d frontend(s) ready", board->name, board->num_frontends);
    return board->num_frontends;

fail:
    for (int j = 0; j < dev->frontend_count; j++) {
        frontend_teardown(&dev->frontends[j]);
    }
    if (dev->bridge) em28xx_close(dev->bridge);
    pthread_mutex_destroy(&dev->bridge_lock);
    if (dev->usb) {
        usbq_release_interface(dev->usb, 0);
        usbq_close(dev->usb);
    }
    free(dev);
    return 0;
}

/* Wrapper for the VID:PID-string entry point: open the first device
 * matching, then delegate to open_device_from_usb. */
static int open_device(const em28xx_board_t *board, const char *vidpid,
                       dvb_frontend_handle_t **out_handles, int max_out) {
    usbq_dev_t *usb = usbq_open(vidpid);
    if (!usb) return 0;
    (void)usbq_disconnect_kernel_driver(usb);
    if (usbq_claim_interface(usb, 0) < 0) {
        ELOG("%s: usbq_claim_interface(0) failed", board->name);
        usbq_close(usb);
        return 0;
    }
    return open_device_from_usb(board, usb, out_handles, max_out);
}

/* ---- Public API -------------------------------------------------- */

int dvb_em28xx_open(const char *vidpid,
                       dvb_frontend_handle_t **handles, int max) {
    if (!vidpid || !handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    const em28xx_board_t *board = find_board_by_vidpid(vidpid);
    if (!board) {
        ELOG("no em28xx board record for %s", vidpid);
        return 0;
    }
    return open_device(board, vidpid, handles, max);
}

int dvb_em28xx_open_by_addr(uint8_t bus_number, uint8_t device_address,
                            dvb_frontend_handle_t **handles, int max) {
    if (!handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }

    usbq_dev_t *usb = usbq_open_by_addr(bus_number, device_address);
    if (!usb) return 0;

    uint16_t vid = 0, pid = 0;
    if (usbq_get_vidpid(usb, &vid, &pid) != 0) {
        usbq_close(usb);
        return 0;
    }
    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", vid, pid);

    const em28xx_board_t *board = find_board_by_vidpid(vidpid);
    if (!board) {
        ELOG("no em28xx board record for %s @ bus %u devaddr %u",
             vidpid, (unsigned)bus_number, (unsigned)device_address);
        usbq_close(usb);
        return 0;
    }

    (void)usbq_disconnect_kernel_driver(usb);
    if (usbq_claim_interface(usb, 0) < 0) {
        ELOG("%s: usbq_claim_interface(0) failed", board->name);
        usbq_close(usb);
        return 0;
    }
    return open_device_from_usb(board, usb, handles, max);
}

int dvb_em28xx_discover_all(dvb_frontend_handle_t **handles, int max) {
    if (!handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    int published = 0;
    for (const em28xx_board_t *b = em28xx_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i] && published < max; i++) {
            int n = open_device(b, b->vidpids[i],
                                &handles[published], max - published);
            published += n;
        }
    }
    return published;
}

/* Lazy-built public view of em28xx_board_table. Same lifetime as
 * the internal table (process), so the static cache is fine. */
const dvb_supported_board_t *dvb_em28xx_supported_boards(int *count_out) {
    static dvb_supported_board_t cache[16];
    static int                   cache_count = -1;

    if (cache_count < 0) {
        int n = 0;
        for (const em28xx_board_t *b = em28xx_board_table;
             b->name && n < (int)(sizeof(cache) / sizeof(cache[0])); b++) {
            cache[n].bridge        = "em28xx";
            cache[n].name          = b->name;
            cache[n].vidpids       = b->vidpids;
            cache[n].num_frontends = b->num_frontends;
            n++;
        }
        cache_count = n;
    }
    if (count_out) *count_out = cache_count;
    return cache;
}

int dvb_em28xx_scan_present(dvb_present_board_t *out, int max) {
    if (!out || max <= 0) return 0;
    /* Pure libusb enumeration. Doesn't need an open device or a
     * configured firmware path — only init'd context. */
    if (usbq_init() != 0) return 0;
    int n = 0;
    for (const em28xx_board_t *b = em28xx_board_table;
         b->name && n < max; b++) {
        for (int i = 0; b->vidpids[i] && n < max; i++) {
            /* Walk every physical USB device matching this VID:PID
             * — multiple instances of the same board produce
             * multiple entries (different bus_number / device_address). */
            usbq_device_info_t devs[16];
            int dn = usbq_enumerate(b->vidpids[i], devs,
                                    (int)(sizeof(devs)/sizeof(devs[0])));
            for (int k = 0; k < dn && n < max; k++) {
                out[n].bridge         = "em28xx";
                out[n].name           = b->name;
                out[n].vidpid         = b->vidpids[i];
                out[n].num_frontends  = b->num_frontends;
                out[n].bus_number     = devs[k].bus_number;
                out[n].device_address = devs[k].device_address;
                n++;
            }
        }
    }
    return n;
}

void dvb_em28xx_shutdown(void) {
    while (g_devices) {
        dvb_em28xx_dev_t *dev = g_devices;
        g_devices = dev->next;

        for (int j = 0; j < dev->frontend_count; j++) {
            frontend_teardown(&dev->frontends[j]);
        }
        if (dev->bridge) em28xx_close(dev->bridge);
        if (dev->usb) {
            usbq_release_interface(dev->usb, 0);
            usbq_close(dev->usb);
        }
        pthread_mutex_destroy(&dev->bridge_lock);
        free(dev);
    }
    if (g_usbq_inited) {
        usbq_shutdown();
        g_usbq_inited = 0;
    }
}
