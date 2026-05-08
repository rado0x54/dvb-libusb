/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * engine_dib0700 — bridge lifecycle for dib0700-based DVB USB
 * devices. Same shape as engine_em28xx; the dib0700-specific
 * parts are firmware upload at cold-detect and the per-board
 * GPIO bring-up sequence (typically GPIO power + reset).
 */

#include "engine_dib0700/engine_dib0700.h"
#include "engine_dib0700_internal.h"

#include "dvb_handle/dvb_debug.h"
#include "dib0700/dib0700.h"
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
        fprintf(stderr, "[engine_dib0700] " __VA_ARGS__);                      \
        fprintf(stderr, "\n");                                                 \
    } while (0)

typedef struct engine_dib0700_frontend {
    struct i2c_client    *demod_client;
    struct dvb_frontend  *fe;
    struct i2c_client    *tuner_client;
    int                   demod_init_done;
    int                   tuner_init_done;
    int                   capture_on;
    int                   fe_index;
    uint8_t               ts_endpoint;
    uint32_t              tune_count;

    usbq_stream_t        *stream;
    int                   wake_pipe[2];

    struct engine_dib0700_dev *dev;
    dvb_frontend_handle_t handle;
} engine_dib0700_frontend_t;

typedef struct engine_dib0700_dev {
    usbq_dev_t                *usb;
    dib0700_dev_t             *bridge;
    pthread_mutex_t            bridge_lock;
    const dib0700_board_t     *board;
    engine_dib0700_frontend_t  frontends[2];   /* current ceiling */
    int                        frontend_count;

    struct engine_dib0700_dev *next;
} engine_dib0700_dev_t;

static engine_dib0700_dev_t *g_devices      = NULL;
static int                   g_usbq_inited  = 0;

/* ---- Vtable thunks ----------------------------------------------- */

static int dib_v_tune(void *state, const dvb_tune_params_t *p) {
    engine_dib0700_frontend_t *fe = state;
    if (!fe || !fe->fe || !p) return -EINVAL;

    fe->tune_count++;
    DVBDBG("dib0700 fn=%d tune#%u entry: delsys=%u freq=%u bw=%u sr=%u stream_id=%d "
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
    DVBDBG("dib0700 fn=%d tune#%u set_frontend rc=%d",
           fe->fe_index, fe->tune_count, rc);
    if (rc < 0) return rc;

    /* Mirror upstream dvb-usb-v2's "kill+resubmit URBs around the
     * streaming_ctrl toggle" pattern. dib0700's bulk-IN endpoint
     * state machine doesn't recover cleanly across enable/disable
     * cycles if the same URBs stay submitted across the toggle —
     * the chip locks but no bytes ever flow on the second+ tune.
     * Cancel + resubmit gives the endpoint a fresh batch. */
    if (fe->stream) {
        int rrc = usbq_stream_restart(fe->stream);
        DVBDBG("dib0700 fn=%d usbq_stream_restart rc=%d", fe->fe_index, rrc);
        if (rrc < 0) {
            /* Non-fatal: degrade to old behaviour rather than abort. */
        }
    }
    usbq_stream_flush(fe->stream);

    if (!fe->capture_on) {
        rc = dib0700_streaming_ctrl(fe->dev->bridge, /*adapter_idx=*/0, 1,
                                    /*disable_master=*/1);
        DVBDBG("dib0700 fn=%d streaming_ctrl(1) rc=%d", fe->fe_index, rc);
        if (rc < 0) return rc;
        fe->capture_on = 1;
    } else {
        DVBDBG("dib0700 fn=%d capture already on (no toggle)", fe->fe_index);
    }
    return 0;
}

static int dib_v_read_ts(void *state, void *buf, size_t len,
                         uint32_t timeout_ms) {
    engine_dib0700_frontend_t *fe = state;
    if (!fe || !buf || len == 0) return -EINVAL;
    if (!fe->stream) return 0;
    return usbq_stream_read(fe->stream, buf, len, timeout_ms);
}

static int dib_v_get_status(void *state, dvb_status_t *out) {
    engine_dib0700_frontend_t *fe = state;
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
    DVBDBG("dib0700 fn=%d status: 0x%02x (%c%c%c%c%c) cnr=%d.%03d dB",
           fe->fe_index, (unsigned)st,
           (st & FE_HAS_SIGNAL)  ? 'S' : '-',
           (st & FE_HAS_CARRIER) ? 'C' : '-',
           (st & FE_HAS_VITERBI) ? 'V' : '-',
           (st & FE_HAS_SYNC)    ? 'Y' : '-',
           (st & FE_HAS_LOCK)    ? 'L' : '-',
           out->cnr_db_x1000 / 1000, abs(out->cnr_db_x1000) % 1000);
    return 0;
}

static int dib_v_event_fd(void *state) {
    engine_dib0700_frontend_t *fe = state;
    return fe ? fe->wake_pipe[0] : -1;
}

static int dib_v_capture_stop(void *state) {
    engine_dib0700_frontend_t *fe = state;
    if (!fe) return -EINVAL;
    if (!fe->capture_on) {
        DVBDBG("dib0700 fn=%d capture_stop: already off", fe->fe_index);
        return 0;
    }
    int rc = dib0700_streaming_ctrl(fe->dev->bridge, /*adapter_idx=*/0, 0,
                                    /*disable_master=*/1);
    DVBDBG("dib0700 fn=%d streaming_ctrl(0) rc=%d (overflow_bytes=%llu)",
           fe->fe_index, rc,
           (unsigned long long)
           (fe->stream ? usbq_stream_overflow_bytes(fe->stream) : 0));
    if (rc < 0) return rc;
    fe->capture_on = 0;
    return 0;
}

static void dib_v_close(void *state) { (void)state; }

static const dvb_engine_vtable_t k_dib0700_vtable = {
    dib_v_tune,
    dib_v_read_ts,
    dib_v_get_status,
    dib_v_event_fd,
    dib_v_capture_stop,
    dib_v_close,
};

/* ---- Bring-up helpers -------------------------------------------- */

static const dib0700_board_t *find_board_by_vidpid(const char *vidpid) {
    if (!vidpid) return NULL;
    for (const dib0700_board_t *b = dib0700_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i]; i++) {
            if (strcmp(b->vidpids[i], vidpid) == 0) return b;
        }
    }
    return NULL;
}

static int frontend_open(engine_dib0700_dev_t *dev, int fn,
                         struct i2c_adapter *parent_adap) {
    engine_dib0700_frontend_t *fe = &dev->frontends[fn];
    fe->dev          = dev;
    fe->fe_index     = fn;
    fe->ts_endpoint  = dev->board->ts_endpoints[fn];
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
                      ? dev->board->ts_buf_sizes[fn]   : 0,
        .pool_depth = dev->board->ts_pool_depths
                      ? dev->board->ts_pool_depths[fn] : 0,
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

    fe->handle.ops          = &k_dib0700_vtable;
    fe->handle.engine_state = fe;
    fe->handle.bridge_lock  = &dev->bridge_lock;
    fe->handle.supported_delsys       = dev->board->supported_delsys;
    fe->handle.supported_delsys_count = dev->board->supported_delsys_count;
    return 0;
}

static void frontend_teardown(engine_dib0700_frontend_t *fe) {
    if (!fe) return;
    if (fe->capture_on && fe->dev && fe->dev->bridge) {
        dib0700_streaming_ctrl(fe->dev->bridge, /*adapter_idx=*/0, 0,
                               /*disable_master=*/1);
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

static int open_device(const dib0700_board_t *board, const char *vidpid,
                       dvb_frontend_handle_t **out_handles, int max_out) {
    if (max_out < board->num_frontends) {
        DLOG("%s: caller has %d slot(s), need %d — skipping",
             board->name, max_out, board->num_frontends);
        return 0;
    }

    usbq_dev_t *usb = usbq_open(vidpid);
    if (!usb) return 0;
    (void)usbq_disconnect_kernel_driver(usb);
    if (usbq_claim_interface(usb, 0) < 0) {
        DLOG("%s: usbq_claim_interface(0) failed", board->name);
        usbq_close(usb);
        return 0;
    }

    engine_dib0700_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        usbq_release_interface(usb, 0);
        usbq_close(usb);
        return 0;
    }
    pthread_mutex_init(&dev->bridge_lock, NULL);
    dev->usb   = usb;
    dev->board = board;

    dev->bridge = dib0700_open(usb);
    if (!dev->bridge) goto fail;

    /* Cold-detect + firmware upload. The polyfill's request_firmware
     * resolution chain is configured by the caller (plugin / test
     * sets linuxdvbkpi_set_firmware_root before opening). We resolve
     * the bridge fw path the same way for now: the caller already
     * pointed FIRMWARE_DIR at it, and dib0700_download_firmware
     * takes a full path, so we look it up via env. */
    const char *fw_dir = getenv("FIRMWARE_DIR");
    /* Fallbacks identical to the polyfill resolver. */
    static const char *fb_dirs[] = {
        "/usr/local/lib/firmware",
        "/usr/lib/firmware",
        "/lib/firmware",
        NULL,
    };
    char fw_path[1024] = {0};
    int  cold = dib0700_is_cold(dev->bridge);
    if (cold < 0) goto fail;
    if (cold) {
        if (fw_dir && fw_dir[0]) {
            snprintf(fw_path, sizeof(fw_path), "%s/%s",
                     fw_dir, board->bridge_firmware);
        } else {
            for (int i = 0; fb_dirs[i]; i++) {
                snprintf(fw_path, sizeof(fw_path), "%s/%s",
                         fb_dirs[i], board->bridge_firmware);
                if (access(fw_path, R_OK) == 0) break;
                fw_path[0] = 0;
            }
        }
        if (!fw_path[0]) {
            DLOG("%s: bridge firmware %s not found in $FIRMWARE_DIR or "
                 "system fallback paths", board->name, board->bridge_firmware);
            goto fail;
        }
        int rc = dib0700_download_firmware(dev->bridge, fw_path);
        if (rc < 0) {
            DLOG("%s: dib0700_download_firmware(%s) failed: %d",
                 board->name, fw_path, rc);
            goto fail;
        }
    }

    uint32_t fw_ver = 0;
    if (dib0700_get_firmware_version(dev->bridge, &fw_ver) == 0) {
        DLOG("%s: dib0700 fw version 0x%08x", board->name, fw_ver);
    }

    if (board->bringup) {
        int rc = board->bringup(dev->bridge);
        if (rc < 0) {
            DLOG("%s: board bring-up failed: %d", board->name, rc);
            goto fail;
        }
    }

    struct i2c_adapter *parent_adap =
        (struct i2c_adapter *)dib0700_get_i2c_adapter(dev->bridge);
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
    if (dev->bridge) dib0700_close(dev->bridge);
    pthread_mutex_destroy(&dev->bridge_lock);
    if (dev->usb) {
        usbq_release_interface(dev->usb, 0);
        usbq_close(dev->usb);
    }
    free(dev);
    return 0;
}

/* ---- Public API -------------------------------------------------- */

int engine_dib0700_open(const char *vidpid,
                        dvb_frontend_handle_t **handles, int max) {
    if (!vidpid || !handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    const dib0700_board_t *board = find_board_by_vidpid(vidpid);
    if (!board) {
        DLOG("no dib0700 board record for %s", vidpid);
        return 0;
    }
    return open_device(board, vidpid, handles, max);
}

int engine_dib0700_discover_all(dvb_frontend_handle_t **handles, int max) {
    if (!handles || max <= 0) return 0;
    if (!g_usbq_inited) {
        if (usbq_init() != 0) return 0;
        g_usbq_inited = 1;
    }
    int published = 0;
    for (const dib0700_board_t *b = dib0700_board_table; b->name; b++) {
        for (int i = 0; b->vidpids[i] && published < max; i++) {
            int n = open_device(b, b->vidpids[i],
                                &handles[published], max - published);
            published += n;
        }
    }
    return published;
}

const dvb_supported_board_t *engine_dib0700_supported_boards(int *count_out) {
    static dvb_supported_board_t cache[16];
    static int                   cache_count = -1;

    if (cache_count < 0) {
        int n = 0;
        for (const dib0700_board_t *b = dib0700_board_table;
             b->name && n < (int)(sizeof(cache) / sizeof(cache[0])); b++) {
            cache[n].bridge        = "dib0700";
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

void engine_dib0700_shutdown(void) {
    while (g_devices) {
        engine_dib0700_dev_t *dev = g_devices;
        g_devices = dev->next;

        for (int j = 0; j < dev->frontend_count; j++) {
            frontend_teardown(&dev->frontends[j]);
        }
        if (dev->bridge) dib0700_close(dev->bridge);
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
