/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_pid_dump — bridge-agnostic TS dumper for any supported DVB
 * USB device. Scans all supported devices across both engines
 * (engine_em28xx + engine_dib0700) at startup, lists what was
 * found, then drives the device at index 0 (or `idx` if given).
 *
 * Standalone tool — no SAT>IP / streaming-server dependency.
 * Useful for hardware diagnostics or piping TS into ffmpeg / VLC.
 *
 *   usage:
 *     dvb_pid_dump --list <fw_dir>
 *     dvb_pid_dump <fw_dir> <delsys> <freq> <bw> [pid] [duration_s] [idx]
 *
 *     fw_dir     : directory containing firmware blobs.
 *     delsys     : dvbt | dvbt2 | dvbc | atsc
 *     pid        : 13-bit TS PID to filter on. -1 = pass all. Default 0.
 *     duration_s : default 5.
 *     idx        : 0-based index into the discovered-devices list.
 *                  Default 0 (first match). Run with --list to see all.
 *
 *   e.g. (single device, dump PAT for 5 s):
 *     dvb_pid_dump ../../artifacts/firmware \
 *       dvbc 386000000 8000000 0 5 > /tmp/pat.ts
 *
 *   e.g. (two devices plugged in — test the second one):
 *     dvb_pid_dump ../../artifacts/firmware \
 *       dvbc 386000000 8000000 0 5 1 > /tmp/pat.ts
 *
 *   e.g. (just enumerate):
 *     dvb_pid_dump --list ../../artifacts/firmware
 */

#include "dvb_handle/dvb_handle.h"
#include "engine_em28xx/engine_em28xx.h"
#include "engine_dib0700/engine_dib0700.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern void linuxdvbkpi_set_firmware_root(const char *path);

#define TS_PACKET 188
#define MAX_HANDLES 8

/* uAPI delivery system enum values — kept here as integers so the
 * test doesn't drag in <linux/dvb/frontend.h>. */
#define SYS_DVBC_ANNEX_A  1
#define SYS_DVBT          3
#define SYS_ATSC          11
#define SYS_DVBT2         16

static int parse_delsys(const char *s, uint32_t *out) {
    if (!strcmp(s, "dvbt"))  { *out = SYS_DVBT;          return 0; }
    if (!strcmp(s, "dvbt2")) { *out = SYS_DVBT2;         return 0; }
    if (!strcmp(s, "dvbc"))  { *out = SYS_DVBC_ANNEX_A;  return 0; }
    if (!strcmp(s, "atsc"))  { *out = SYS_ATSC;          return 0; }
    return -1;
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000L + (long)(t.tv_nsec / 1000000L);
}

static uint16_t ts_pid(const uint8_t *pkt) {
    return (uint16_t)(((pkt[1] & 0x1f) << 8) | pkt[2]);
}

/* Discover everything plugged in across both engines and append
 * to handles[]. Returns total handles published. */
static int discover_all(dvb_frontend_handle_t **handles, int max) {
    int n = 0;
    n += engine_em28xx_discover_all (&handles[n], max - n);
    n += engine_dib0700_discover_all(&handles[n], max - n);
    return n;
}

static void list_handles(dvb_frontend_handle_t **handles, int n) {
    fprintf(stderr, "discovered %d frontend(s):\n", n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  [%d] %s\n", i, handles[i]->display_name);
    }
}

static void shutdown_all(void) {
    /* Mirror the plugin's reverse-of-discovery shutdown order. */
    engine_dib0700_shutdown();
    engine_em28xx_shutdown();
}

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s --list <fw_dir>\n"
            "  %s <fw_dir> <dvbt|dvbt2|dvbc|atsc> <freq_hz> <bw_hz> "
            "[pid|-1] [duration_s] [idx]\n",
            argv0, argv0);
    return 2;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return usage(argv[0]);

    /* --list mode: just enumerate and exit. */
    if (!strcmp(argv[1], "--list")) {
        if (argc != 3) return usage(argv[0]);
        const char *fw_dir = argv[2];
        linuxdvbkpi_set_firmware_root(fw_dir);
        setenv("FIRMWARE_DIR", fw_dir, /*overwrite=*/1);

        dvb_frontend_handle_t *handles[MAX_HANDLES] = {0};
        int n = discover_all(handles, MAX_HANDLES);
        if (n == 0) {
            fprintf(stderr, "no supported DVB devices found\n");
        } else {
            list_handles(handles, n);
        }
        shutdown_all();
        return n > 0 ? 0 : 1;
    }

    /* Streaming mode. */
    if (argc < 5 || argc > 8) return usage(argv[0]);
    const char *fw_dir = argv[1];
    uint32_t delsys;
    if (parse_delsys(argv[2], &delsys) < 0) {
        fprintf(stderr, "unknown delsys '%s'\n", argv[2]);
        return 2;
    }
    uint32_t freq_hz    = (uint32_t)strtoul(argv[3], NULL, 0);
    uint32_t bw_hz      = (uint32_t)strtoul(argv[4], NULL, 0);
    int      filter_pid = (argc >= 6) ? atoi(argv[5]) : 0;
    int      duration_s = (argc >= 7) ? atoi(argv[6]) : 5;
    int      idx        = (argc >= 8) ? atoi(argv[7]) : 0;

    /* Plumb firmware dir into the polyfill BEFORE engine open. The
     * dib0700 engine reads $FIRMWARE_DIR directly for its bridge
     * ramcode upload; export so it's visible to that path too. */
    linuxdvbkpi_set_firmware_root(fw_dir);
    setenv("FIRMWARE_DIR", fw_dir, /*overwrite=*/1);

    int success = 0;
    dvb_frontend_handle_t *handles[MAX_HANDLES] = {0};
    int n = discover_all(handles, MAX_HANDLES);
    if (n == 0) {
        fprintf(stderr, "no supported DVB devices found\n");
        shutdown_all();
        return 1;
    }
    list_handles(handles, n);

    if (idx < 0 || idx >= n) {
        fprintf(stderr, "device index %d out of range (have 0..%d)\n",
                idx, n - 1);
        shutdown_all();
        return 1;
    }

    dvb_frontend_handle_t *fe = handles[idx];
    fprintf(stderr, "using [%d] %s\n", idx, fe->display_name);

    dvb_tune_params_t tp = {
        .delsys       = delsys,
        .freq_hz      = freq_hz,
        .bandwidth_hz = bw_hz,
        .symbol_rate  = 6900000u,    /* DVB-C default; ignored elsewhere */
        .stream_id    = -1,
    };
    int rc = fe->ops->tune(fe->engine_state, &tp);
    if (rc < 0) {
        fprintf(stderr, "tune failed: %d\n", rc);
        goto done;
    }

    /* Wait for lock — up to 3 s. */
    dvb_status_t st = {0};
    long lock_deadline = now_ms() + 3000;
    while (now_ms() < lock_deadline) {
        if (fe->ops->get_status(fe->engine_state, &st) == 0 && st.has_lock) break;
        struct timespec sl = { 0, 100 * 1000000L };
        nanosleep(&sl, NULL);
    }
    if (!st.has_lock) {
        fprintf(stderr, "no lock after 3 s\n");
        goto done;
    }
    fprintf(stderr, "locked, cnr=%d.%03d dB\n",
            st.cnr_db_x1000 / 1000, abs(st.cnr_db_x1000) % 1000);

    /* Read + frame. Standard staging-buffer + sync-find logic. */
    enum { READ_SZ = 48 * 1024 };
    enum { STAGE_SZ = READ_SZ + TS_PACKET };
    uint8_t *stage = malloc(STAGE_SZ);
    if (!stage) { fprintf(stderr, "OOM\n"); goto done; }
    size_t stage_len = 0;
    int    locked    = 0;

    uint64_t total_packets    = 0;
    uint64_t matched_packets  = 0;
    uint64_t bad_sync_packets = 0;

    long t_end = now_ms() + (long)duration_s * 1000;
    int  timeout_ms = 1000;

    while (now_ms() < t_end) {
        size_t want = STAGE_SZ - stage_len;
        if (want > READ_SZ) want = READ_SZ;
        int got = fe->ops->read_ts(fe->engine_state,
                                   stage + stage_len, want, timeout_ms);
        if (got < 0) {
            fprintf(stderr, "read_ts error: %d\n", got);
            break;
        }
        if (got == 0) continue;
        stage_len += (size_t)got;
        timeout_ms = 100;

        if (!locked) {
            ssize_t off = -1;
            for (size_t i = 0; i + TS_PACKET < stage_len; i++) {
                if (stage[i] == 0x47 && stage[i + TS_PACKET] == 0x47) {
                    off = (ssize_t)i;
                    break;
                }
            }
            if (off < 0) {
                if (stage_len > 2 * TS_PACKET) {
                    size_t keep = 2 * TS_PACKET;
                    memmove(stage, stage + stage_len - keep, keep);
                    stage_len = keep;
                }
                continue;
            }
            if (off > 0) {
                memmove(stage, stage + off, stage_len - off);
                stage_len -= (size_t)off;
            }
            locked = 1;
            fprintf(stderr, "TS sync at offset %zd\n", off);
        }

        size_t consumed = 0;
        while (consumed + TS_PACKET <= stage_len) {
            const uint8_t *pkt = stage + consumed;
            if (pkt[0] != 0x47) {
                bad_sync_packets++;
                size_t resync = consumed;
                ssize_t off = -1;
                while (resync + TS_PACKET < stage_len) {
                    if (stage[resync] == 0x47 &&
                        stage[resync + TS_PACKET] == 0x47) {
                        off = (ssize_t)(resync - consumed);
                        break;
                    }
                    resync++;
                }
                if (off < 0) {
                    consumed = stage_len;
                    locked   = 0;
                    break;
                }
                consumed += (size_t)off;
                continue;
            }
            uint16_t pid = ts_pid(pkt);
            total_packets++;
            if (filter_pid < 0 || (int)pid == filter_pid) {
                matched_packets++;
                if (write(STDOUT_FILENO, pkt, TS_PACKET) != TS_PACKET) {
                    fprintf(stderr, "write to stdout failed: %d\n", errno);
                    goto stream_done;
                }
            }
            consumed += TS_PACKET;
        }
        if (consumed > 0) {
            size_t tail = stage_len - consumed;
            if (tail > 0) memmove(stage, stage + consumed, tail);
            stage_len = tail;
        }
    }
stream_done:
    free(stage);

    fprintf(stderr,
            "summary: total=%" PRIu64 " matched=%" PRIu64 " bad_sync=%" PRIu64
            " (filter=%d, %ds)\n",
            total_packets, matched_packets, bad_sync_packets,
            filter_pid, duration_s);
    success = (matched_packets > 0 &&
               bad_sync_packets * 100 < total_packets);

done:
    shutdown_all();
    return success ? 0 : 1;
}
