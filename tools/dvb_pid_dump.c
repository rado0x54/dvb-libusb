/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_pid_dump — bridge-agnostic TS dumper for any supported DVB
 * USB device. Scans the bus, opens the device at index 0 (or `idx`
 * if given), tunes, and dumps 188-aligned TS packets — optionally
 * filtered to a set of PIDs — to stdout.
 *
 * Standalone tool — no SAT>IP / streaming-server dependency.
 *
 *   usage:
 *     dvb_pid_dump --list
 *     dvb_pid_dump <delsys> <freq_hz> <bw_hz> [pids] [duration_s] [idx]
 *
 *     delsys     : dvbt | dvbt2 | dvbc | atsc
 *     pids       : -1 = pass all (default 0). A single PID, or a
 *                  comma-separated list ("0,17,18,5100,5101"), keeps
 *                  only matching packets.
 *     duration_s : default 5.
 *     idx        : 0-based index into the discovered-devices list.
 *                  Default 0 (first match). Run with --list to see all.
 *
 *   e.g. (dump PAT for 5 s):
 *     dvb_pid_dump dvbc 386000000 8000000 0 5 > /tmp/pat.ts
 *
 *   e.g. (two devices plugged in — test the second one):
 *     dvb_pid_dump dvbc 386000000 8000000 0 5 1 > /tmp/pat.ts
 *
 * Firmware lookup (matches the kernel's request_firmware contract,
 * via linuxdvbkpi):
 *   1. $FIRMWARE_DIR  (env override — set this if your blobs are
 *                      somewhere else)
 *   2. /usr/local/lib/firmware
 *   3. /usr/lib/firmware
 *   4. /lib/firmware  (the kernel default)
 * Drop blobs in any of those and the tool finds them automatically.
 * If a blob is missing, the engine's open() prints a clear error and
 * exits non-zero.
 */

#include "dvb_handle/dvb_handle.h"
#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_dib0700/dvb_dib0700.h"
#include "dvb_dvbsky/dvb_dvbsky.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/* Up to 32 PIDs in a single --pids list — covers a typical service
 * (PAT/CAT/PMT + video + audio + subtitle + a few extras). */
#define MAX_FILTER_PIDS 32

typedef struct pid_filter {
    int      pass_all;        /* -1 short-form */
    int      n;
    uint16_t pids[MAX_FILTER_PIDS];
} pid_filter_t;

static int parse_pid_filter(const char *s, pid_filter_t *out) {
    memset(out, 0, sizeof(*out));
    if (!s || !*s) { out->pass_all = 0; out->n = 1; out->pids[0] = 0; return 0; }
    /* "-1" → pass everything. */
    if (!strcmp(s, "-1")) { out->pass_all = 1; return 0; }

    /* Comma-separated list, optionally just a single integer. */
    const char *p = s;
    while (*p) {
        char *end = NULL;
        long v = strtol(p, &end, 0);
        if (end == p) return -1;
        if (v < 0 || v > 0x1fff) return -1;
        if (out->n >= MAX_FILTER_PIDS) return -1;
        out->pids[out->n++] = (uint16_t)v;
        p = end;
        if (*p == ',') p++;
        else if (*p)  return -1;
    }
    return out->n > 0 ? 0 : -1;
}

static int pid_filter_match(const pid_filter_t *f, uint16_t pid) {
    if (f->pass_all) return 1;
    for (int i = 0; i < f->n; i++) if (f->pids[i] == pid) return 1;
    return 0;
}

/* Discover everything plugged in across both engines and append
 * to handles[]. Returns total handles published. */
static int discover_all(dvb_frontend_handle_t **handles, int max) {
    int n = 0;
    n += dvb_em28xx_discover_all (&handles[n], max - n);
    n += dvb_dib0700_discover_all(&handles[n], max - n);
    n += dvb_dvbsky_discover_all (&handles[n], max - n);
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
    dvb_dvbsky_shutdown();
    dvb_dib0700_shutdown();
    dvb_em28xx_shutdown();
}

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s --list\n"
            "  %s <dvbt|dvbt2|dvbc|atsc> <freq_hz> <bw_hz> "
            "[pids] [duration_s] [idx]\n"
            "\n"
            "  pids: -1 = pass all (default 0). Single PID, or a\n"
            "        comma-separated list like \"0,17,18,5100,5101\".\n"
            "\n"
            "Firmware: $FIRMWARE_DIR if set, else /usr/local/lib/firmware,\n"
            "          /usr/lib/firmware, /lib/firmware (kernel default).\n",
            argv0, argv0);
    return 2;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return usage(argv[0]);

    /* --list mode: just enumerate and exit. */
    if (!strcmp(argv[1], "--list")) {
        if (argc != 2) return usage(argv[0]);

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
    if (argc < 4 || argc > 7) return usage(argv[0]);
    uint32_t delsys;
    if (parse_delsys(argv[1], &delsys) < 0) {
        fprintf(stderr, "unknown delsys '%s'\n", argv[1]);
        return 2;
    }
    uint32_t freq_hz    = (uint32_t)strtoul(argv[2], NULL, 0);
    uint32_t bw_hz      = (uint32_t)strtoul(argv[3], NULL, 0);
    pid_filter_t filter;
    if (parse_pid_filter(argc >= 5 ? argv[4] : "0", &filter) < 0) {
        fprintf(stderr, "bad pids argument: '%s'\n", argv[4]);
        return 2;
    }
    int      duration_s = (argc >= 6) ? atoi(argv[5]) : 5;
    int      idx        = (argc >= 7) ? atoi(argv[6]) : 0;

    /* Firmware: rely on the polyfill's auto-find chain
     * ($FIRMWARE_DIR → /usr/local/lib/firmware → /usr/lib/firmware →
     * /lib/firmware). The tool doesn't override the path; if blobs
     * aren't on disk somewhere, the engine open() returns a clear
     * error. */

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
            if (pid_filter_match(&filter, pid)) {
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

    char filter_repr[160] = {0};
    if (filter.pass_all) {
        snprintf(filter_repr, sizeof(filter_repr), "all");
    } else {
        size_t off = 0;
        for (int i = 0; i < filter.n; i++) {
            int wrote = snprintf(filter_repr + off, sizeof(filter_repr) - off,
                                 "%s%u", i ? "," : "", (unsigned)filter.pids[i]);
            if (wrote < 0 || (size_t)wrote >= sizeof(filter_repr) - off) break;
            off += (size_t)wrote;
        }
    }
    fprintf(stderr,
            "summary: total=%" PRIu64 " matched=%" PRIu64 " bad_sync=%" PRIu64
            " (filter=%s, %ds)\n",
            total_packets, matched_packets, bad_sync_packets,
            filter_repr, duration_s);
    success = (matched_packets > 0 &&
               bad_sync_packets * 100 < total_packets);

done:
    shutdown_all();
    return success ? 0 : 1;
}
