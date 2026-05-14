/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_dvbsky — bridge lifecycle for dvbsky-based DVB USB devices.
 * Same shape as dvb_dib0700; the dvbsky-specific parts are
 *
 *   (a) no bridge firmware upload — the chip ships warm.
 *
 *   (b) a "fifo resync on lock-edge" hook on the status path: every
 *       time the demod transitions from unlocked → locked, we re-fire
 *       the bridge's stream-enable command. Upstream's
 *       dvbsky_usb_read_status does the same; the comment there says
 *       it's needed to resync the slave fifo after lock.
 *
 *   (c) streaming_ctrl is unconditional on tune (no need for
 *       usbq_stream_restart — the chip handles the toggle cleanly
 *       without the dib0700-style URB kill/resubmit dance).
 */

#include "dvb_dvbsky/dvb_dvbsky.h"
#include "dvb_dvbsky_priv.h"

#include "dvb_handle/dvb_debug.h"
#include "dvbsky/dvbsky.h"
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

#define DLOG(...)                                                              \
    do {                                                                       \
        fprintf(stderr, "[dvb_dvbsky] " __VA_ARGS__);                          \
        fprintf(stderr, "\n");                                                 \
    } while (0)

typedef struct dvb_dvbsky_frontend {
    struct i2c_client    *demod_client;
    struct dvb_frontend  *fe;
    struct i2c_client    *tuner_client;
    int                   demod_init_done;
    int                   tuner_init_done;
    int                   capture_on;
    int                   last_lock;
    int                   fe_index;
    uint8_t               ts_endpoint;
    uint32_t              tune_count;

    usbq_stream_t        *stream;
    int                   wake_pipe[2];

    struct dvb_dvbsky_dev *dev;
    dvb_frontend_handle_t handle;
} dvb_dvbsky_frontend_t;

typedef struct dvb_dvbsky_dev {
    usbq_dev_t                *usb;
    dvbsky_dev_t              *bridge;
    pthread_mutex_t            bridge_lock;
    const dvbsky_board_t      *board;
    dvb_dvbsky_frontend_t      frontends[1];   /* dvbsky boards are single-frontend today */
    int                        frontend_count;

    struct dvb_dvbsky_dev     *next;
} dvb_dvbsky_dev_t;

static dvb_dvbsky_dev_t *g_devices     = NULL;
static int               g_usbq_inited = 0;

/* ---- Vtable thunks ----------------------------------------------- */

static int dvbsky_v_tune(void *state, const dvb_tune_params_t *p) {
    dvb_dvbsky_frontend_t *fe = state;
    if (!fe || !fe->fe || !p) return -EINVAL;

    fe->tune_count++;
    DVBDBG("dvbsky fn=%d tune#%u entry: delsys=%u freq=%u bw=%u sr=%u stream_id=%d "
           "(prev capture_on=%d, prev overflow=%llu)",
           fe->fe_index, fe->tune_count, p->delsys, p->freq_hz,
           p->bandwidth_hz, p->symbol_rate, p->stream_id, fe->capture_on,
           (unsigned long long)
           (fe->stream ? usbq_stream_overflow_bytes(fe->stream) : 0));

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
    DVBDBG("dvbsky fn=%d tune#%u set_frontend rc=%d",
           fe->fe_index, fe->tune_count, rc);
    if (rc < 0) return rc;

    /* Drop stale ring bytes from the previous tune so the consumer's
     * first read_ts after retune sees only post-tune data. */
    if (fe->stream) usbq_stream_flush(fe->stream);

    /* Bridge-side stream-enable. Upstream's dvbsky_streaming_ctrl
     * is invoked by the dvb-usb-v2 framework around set_frontend;
     * we collapse that into a single post-tune call. Re-firing on
     * lock-edge is handled in get_status. */
    fe->last_lock = 0;
    if (!fe->capture_on) {
        rc = dvbsky_stream_ctrl(fe->dev->bridge, 1);
        DVBDBG("dvbsky fn=%d stream_ctrl(1) rc=%d", fe->fe_index, rc);
        if (rc < 0) return rc;
        fe->capture_on = 1;
    } else {
        /* Re-fire on every retune — the chip's slave FIFO benefits
         * from the stop+start pair when the frequency moves. */
        rc = dvbsky_stream_ctrl(fe->dev->bridge, 1);
        DVBDBG("dvbsky fn=%d stream_ctrl(retune) rc=%d", fe->fe_index, rc);
        if (rc < 0) return rc;
    }
    return 0;
}

static int dvbsky_v_read_ts(void *state, void *buf, size_t len,
                            uint32_t timeout_ms) {
    dvb_dvbsky_frontend_t *fe = state;
    if (!fe || !buf || len == 0) return -EINVAL;
    if (!fe->stream) return 0;
    return usbq_stream_read(fe->stream, buf, len, timeout_ms);
}

static int dvbsky_v_get_status(void *state, dvb_status_t *out) {
    dvb_dvbsky_frontend_t *fe = state;
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

    out->cnr_db_x1000 = 0;
    struct dtv_frontend_properties *c = &fe->fe->dtv_property_cache;
    if (c->cnr.len > 0 && c->cnr.stat[0].scale == FE_SCALE_DECIBEL) {
        out->cnr_db_x1000 = (int32_t)c->cnr.stat[0].svalue;
    }
    DVBDBG("dvbsky fn=%d status: 0x%02x (%c%c%c%c%c) cnr=%d.%03d dB",
           fe->fe_index, (unsigned)st,
           (st & FE_HAS_SIGNAL)  ? 'S' : '-',
           (st & FE_HAS_CARRIER) ? 'C' : '-',
           (st & FE_HAS_VITERBI) ? 'V' : '-',
           (st & FE_HAS_SYNC)    ? 'Y' : '-',
           (st & FE_HAS_LOCK)    ? 'L' : '-',
           out->cnr_db_x1000 / 1000, abs(out->cnr_db_x1000) % 1000);

    /* Lock-edge fifo resync. Mirrors upstream's
     * dvbsky_usb_read_status: when the demod transitions from
     * unlocked → locked, re-fire the bridge stream-enable so the
     * slave fifo restarts on the locked-stream timing. */
    int lock_now = !!(st & FE_HAS_LOCK);
    if (lock_now && !fe->last_lock && fe->capture_on && fe->dev) {
        int srr = dvbsky_stream_ctrl(fe->dev->bridge, 1);
        DVBDBG("dvbsky fn=%d lock-edge resync rc=%d", fe->fe_index, srr);
    }
    fe->last_lock = lock_now;
    return 0;
}

static int dvbsky_v_event_fd(void *state) {
    dvb_dvbsky_frontend_t *fe = state;
    return fe ? fe->wake_pipe[0] : -1;
}

static int dvbsky_v_capture_stop(void *state) {
    dvb_dvbsky_frontend_t *fe = state;
    if (!fe) return -EINVAL;
    if (!fe->capture_on) {
        DVBDBG("dvbsky fn=%d capture_stop: already off", fe->fe_index);
        return 0;
    }
    int rc = dvbsky_stream_ctrl(fe->dev->bridge, 0);
    DVBDBG("dvbsky fn=%d stream_ctrl(0) rc=%d (overflow_bytes=%llu)",
           fe->fe_index, rc,
           (unsigned long long)
           (fe->stream ? usbq_stream_overflow_bytes(fe->stream) : 0));
    if (rc < 0) return rc;
    fe->capture_on = 0;
    fe->last_lock  = 0;
    return 0;
}

static void dvbsky_v_close(void *state) { (void)state; }

static const dvb_engine_vtable_t k_dvbsky_vtable = {
    dvbsky_v_tune,
    dvbsky_v_read_ts,
    dvbsky_v_get_status,
    dvbsky_v_event_fd,
    dvbsky_v_capture_stop,
    dvbsky_v_close,
};

/* ---- Bring-up helpers -------------------------------------------- */

static const dvbsky_board_t *find_board_by_vidpid(const char *vidpid) {
    if (!vidpid) return NULL;
    for (const dvbsky_board_t *b = dvbsky_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i]; i++) {
            if (strcmp(b->vidpids[i], vidpid) == 0) return b;
        }
    }
    return NULL;
}

static int frontend_open(dvb_dvbsky_dev_t *dev, int fn,
                         struct i2c_adapter *parent_adap) {
    dvb_dvbsky_frontend_t *fe = &dev->frontends[fn];
    fe->dev          = dev;
    fe->fe_index     = fn;
    fe->ts_endpoint  = dev->board->ts_endpoints
                       ? dev->board->ts_endpoints[fn]
                       : DVBSKY_TS_BULK_EP;
    fe->wake_pipe[0] = -1;
    fe->wake_pipe[1] = -1;

    if (pipe(fe->wake_pipe) < 0) return -errno;
    fcntl(fe->wake_pipe[0], F_SETFL,
          fcntl(fe->wake_pipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(fe->wake_pipe[1], F_SETFL,
          fcntl(fe->wake_pipe[1], F_GETFL) | O_NONBLOCK);

    int rc = dev->board->attach(parent_adap, fn,
                                &fe->demod_client, &fe->fe,
                                &fe->tuner_client);
    if (rc < 0) {
        DLOG("%s: attach(fn=%d) failed: %d", dev->board->name, fn, rc);
        return rc;
    }

    if (fe->fe->ops.init) {
        rc = fe->fe->ops.init(fe->fe);
        if (rc < 0) return rc;
        fe->demod_init_done = 1;
    }
    if (fe->fe->ops.tuner_ops.init) {
        rc = fe->fe->ops.tuner_ops.init(fe->fe);
        if (rc < 0) return rc;
        fe->tuner_init_done = 1;
    }

    usbq_stream_cfg_t scfg = {
        .endpoint   = fe->ts_endpoint,
        .buf_size   = dev->board->ts_buf_sizes
                      ? dev->board->ts_buf_sizes[fn]
                      : DVBSKY_TS_URB_BYTES,
        .pool_depth = dev->board->ts_pool_depths
                      ? dev->board->ts_pool_depths[fn]
                      : DVBSKY_TS_POOL_DEPTH,
        .wake_fd    = fe->wake_pipe[1],
    };
    fe->stream = usbq_stream_open(dev->usb, &scfg);
    if (!fe->stream) {
        DLOG("%s: usbq_stream_open(ep=0x%02x) failed",
             dev->board->name, fe->ts_endpoint);
        return -EIO;
    }

    char name_buf[96];
    if (dev->board->num_frontends > 1) {
        snprintf(name_buf, sizeof(name_buf), "%s (fe%d)",
                 dev->board->name, fn);
    } else {
        snprintf(name_buf, sizeof(name_buf), "%s", dev->board->name);
    }
    fe->handle.display_name = strdup(name_buf);

    fe->handle.ops          = &k_dvbsky_vtable;
    fe->handle.engine_state = fe;
    fe->handle.bridge_lock  = &dev->bridge_lock;
    fe->handle.supported_delsys       = dev->board->supported_delsys;
    fe->handle.supported_delsys_count = dev->board->supported_delsys_count;
    (void)usbq_get_bus_devaddr(dev->usb,
                               &fe->handle.bus_number,
                               &fe->handle.device_address);
    return 0;
}

static void frontend_teardown(dvb_dvbsky_frontend_t *fe) {
    if (!fe) return;
    if (fe->capture_on && fe->dev && fe->dev->bridge) {
        dvbsky_stream_ctrl(fe->dev->bridge, 0);
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
    if (fe->tuner_client) i2c_unregister_device(fe->tuner_client);
    if (fe->demod_client) i2c_unregister_device(fe->demod_client);
    if (fe->wake_pipe[1] >= 0) { close(fe->wake_pipe[1]); fe->wake_pipe[1] = -1; }
    if (fe->wake_pipe[0] >= 0) { close(fe->wake_pipe[0]); fe->wake_pipe[0] = -1; }
    free((void *)fe->handle.display_name);
    fe->handle.display_name = NULL;
}

static int open_device_from_usb(const dvbsky_board_t *board, usbq_dev_t *usb,
                                dvb_frontend_handle_t **out_handles, int max_out) {
    if (max_out < board->num_frontends) {
        DLOG("%s: caller has %d slot(s), need %d — skipping",
             board->name, max_out, board->num_frontends);
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }

    dvb_dvbsky_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }
    pthread_mutex_init(&dev->bridge_lock, NULL);
    dev->usb   = usb;
    dev->board = board;

    dev->bridge = dvbsky_open(usb);
    if (!dev->bridge) goto fail;

    if (board->bringup) {
        int rc = board->bringup(dev->bridge);
        if (rc < 0) {
            DLOG("%s: board bring-up failed: %d", board->name, rc);
            goto fail;
        }
    }

    struct i2c_adapter *parent_adap =
        (struct i2c_adapter *)dvbsky_get_i2c_adapter(dev->bridge);
    if (!parent_adap) goto fail;

    for (int fn = 0; fn < board->num_frontends; fn++) {
        if (frontend_open(dev, fn, parent_adap) < 0) {
            for (int j = 0; j < fn; j++) frontend_teardown(&dev->frontends[j]);
            goto fail;
        }
        dev->frontend_count = fn + 1;
    }

    for (int fn = 0; fn < board->num_frontends; fn++) {
        out_handles[fn] = &dev->frontends[fn].handle;
    }
    dev->next = g_devices;
    g_devices = dev;

    DLOG("%s: %d frontend(s) ready", board->name, board->num_frontends);
    return board->num_frontends;

fail:
    for (int j = 0; j < dev->frontend_count; j++) {
        frontend_teardown(&dev->frontends[j]);
    }
    if (dev->bridge) dvbsky_close(dev->bridge);
    pthread_mutex_destroy(&dev->bridge_lock);
    if (dev->usb) {
        usbq_release_interface(dev->usb, 0);
        usbq_close(dev->usb);
    }
    free(dev);
    return 0;
}

static int open_device(const dvbsky_board_t *board, const char *vidpid,
                       dvb_frontend_handle_t **out_handles, int max_out) {
    usbq_dev_t *usb = usbq_open(vidpid);
    if (!usb) return 0;
    (void)usbq_disconnect_kernel_driver(usb);
    if (usbq_claim_interface(usb, 0) < 0) {
        DLOG("%s: usbq_claim_interface(0) failed", board->name);
        usbq_close(usb);
        return 0;
    }
    return open_device_from_usb(board, usb, out_handles, max_out);
}

/* ---- Public API -------------------------------------------------- */

int dvb_dvbsky_open(const char *vidpid,
                    dvb_frontend_handle_t **handles, int max) {
    if (!vidpid || !handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    const dvbsky_board_t *board = find_board_by_vidpid(vidpid);
    if (!board) {
        DLOG("no dvbsky board record for %s", vidpid);
        return 0;
    }
    return open_device(board, vidpid, handles, max);
}

int dvb_dvbsky_open_by_addr(uint8_t bus_number, uint8_t device_address,
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

    const dvbsky_board_t *board = find_board_by_vidpid(vidpid);
    if (!board) {
        DLOG("no dvbsky board record for %s @ bus %u devaddr %u",
             vidpid, (unsigned)bus_number, (unsigned)device_address);
        usbq_close(usb);
        return 0;
    }

    (void)usbq_disconnect_kernel_driver(usb);
    if (usbq_claim_interface(usb, 0) < 0) {
        DLOG("%s: usbq_claim_interface(0) failed", board->name);
        usbq_close(usb);
        return 0;
    }
    return open_device_from_usb(board, usb, handles, max);
}

int dvb_dvbsky_discover_all(dvb_frontend_handle_t **handles, int max) {
    if (!handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    int published = 0;
    for (const dvbsky_board_t *b = dvbsky_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i] && published < max; i++) {
            int n = open_device(b, b->vidpids[i],
                                &handles[published], max - published);
            published += n;
        }
    }
    return published;
}

const dvb_supported_board_t *dvb_dvbsky_supported_boards(int *count_out) {
    static dvb_supported_board_t cache[16];
    static int                   cache_count = -1;

    if (cache_count < 0) {
        int n = 0;
        for (const dvbsky_board_t *b = dvbsky_board_table;
             b->name && n < (int)(sizeof(cache) / sizeof(cache[0])); b++) {
            cache[n].bridge        = "dvbsky";
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

int dvb_dvbsky_scan_present(dvb_present_board_t *out, int max) {
    if (!out || max <= 0) return 0;
    if (usbq_init() != 0) return 0;
    int n = 0;
    for (const dvbsky_board_t *b = dvbsky_board_table;
         b->name && n < max; b++) {
        for (int i = 0; b->vidpids[i] && n < max; i++) {
            usbq_device_info_t devs[16];
            int dn = usbq_enumerate(b->vidpids[i], devs,
                                    (int)(sizeof(devs)/sizeof(devs[0])));
            for (int k = 0; k < dn && n < max; k++) {
                out[n].bridge         = "dvbsky";
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

void dvb_dvbsky_shutdown(void) {
    while (g_devices) {
        dvb_dvbsky_dev_t *dev = g_devices;
        g_devices = dev->next;

        for (int j = 0; j < dev->frontend_count; j++) {
            frontend_teardown(&dev->frontends[j]);
        }
        if (dev->bridge) dvbsky_close(dev->bridge);
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
