/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_devices — list DVB hardware that's plugged in (default), the
 * build's full board table (--supported), or live-watch arrivals
 * and departures via libusb hotplug (--watch).
 *
 * Default mode (no args): scan USB and print one entry per physical
 * device that matches a board this build knows. Two of the same
 * board show up as two entries (different bus/address). Pure
 * libusb enumeration — no firmware required, no device claim, no
 * chip bring-up.
 *
 *   dvb_devices                # plugged-in devices (default)
 *   dvb_devices --supported    # supported board table (no USB scan)
 *   dvb_devices --watch        # initial scan, then live hotplug stream
 *
 * Watch mode prints the same column layout with a leading +/-
 * marker on hotplug events. It never opens a device, so idle
 * arrivals/departures are detected purely via libusb's hotplug
 * callbacks (USB bus events, not streaming activity). Ctrl-C exits.
 *
 * Firmware: this tool never opens a device, so firmware is
 * irrelevant here. It matters for `dvb_pid_dump`. The library's
 * lookup chain is `linuxdvbkpi_set_firmware_root()` → $FIRMWARE_DIR
 * → /usr/local/lib/firmware → /usr/lib/firmware → /lib/firmware.
 */

#include "dvb_handle/dvb_handle.h"
#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_dib0700/dvb_dib0700.h"
#include "dvb_dvbsky/dvb_dvbsky.h"
#include "dvb_hotplug/dvb_hotplug.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_BOARDS 32

static int gather_supported(const dvb_supported_board_t *out[], int max) {
    int n = 0;
    int em_n = 0, dib_n = 0, sky_n = 0;
    const dvb_supported_board_t *em  = dvb_em28xx_supported_boards (&em_n);
    const dvb_supported_board_t *dib = dvb_dib0700_supported_boards(&dib_n);
    const dvb_supported_board_t *sky = dvb_dvbsky_supported_boards (&sky_n);
    for (int i = 0; i < em_n  && n < max; i++) out[n++] = &em [i];
    for (int i = 0; i < dib_n && n < max; i++) out[n++] = &dib[i];
    for (int i = 0; i < sky_n && n < max; i++) out[n++] = &sky[i];
    return n;
}

static int gather_present(dvb_present_board_t *out, int max) {
    int n = 0;
    n += dvb_em28xx_scan_present (&out[n], max - n);
    n += dvb_dib0700_scan_present(&out[n], max - n);
    n += dvb_dvbsky_scan_present (&out[n], max - n);
    return n;
}

static int print_detected(void) {
    dvb_present_board_t present[MAX_BOARDS] = {0};
    int n = gather_present(present, MAX_BOARDS);
    printf("Detected devices (%d):\n", n);
    if (n == 0) {
        printf("  (no supported hardware plugged in — "
               "run `dvb_devices --supported` to see what this build can drive)\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        printf("  [%-7s] %s — USB %s @ bus %u dev %u, %d frontend%s\n",
               present[i].bridge, present[i].name, present[i].vidpid,
               (unsigned)present[i].bus_number,
               (unsigned)present[i].device_address,
               present[i].num_frontends,
               present[i].num_frontends == 1 ? "" : "s");
    }
    return 0;
}

static int print_supported(void) {
    const dvb_supported_board_t *supported[MAX_BOARDS] = {0};
    int n = gather_supported(supported, MAX_BOARDS);
    printf("Supported devices in this build (%d):\n", n);
    for (int i = 0; i < n; i++) {
        const dvb_supported_board_t *b = supported[i];
        printf("  [%-7s] %s — %d frontend%s\n",
               b->bridge, b->name, b->num_frontends,
               b->num_frontends == 1 ? "" : "s");
        for (int j = 0; b->vidpids && b->vidpids[j]; j++) {
            printf("      USB %s\n", b->vidpids[j]);
        }
    }
    return 0;
}

/* ---- --watch -------------------------------------------------------- */

static volatile sig_atomic_t g_watch_stop;
static void on_sigint(int sig) { (void)sig; g_watch_stop = 1; }

/* Resolve an incoming hotplug event back to a board name + bridge
 * string by walking the same supported-boards aggregate the default
 * mode uses. Returns NULL board if the VID:PID isn't in any table
 * (shouldn't happen — the hotplug filter rejects unknown VID:PIDs —
 * but defensive). */
static void event_lookup(const dvb_hotplug_event_t *ev,
                         const char **board_name_out,
                         const char **bridge_str_out) {
    const dvb_supported_board_t *supported[MAX_BOARDS] = {0};
    int n = gather_supported(supported, MAX_BOARDS);
    char want[16];
    snprintf(want, sizeof(want), "%04x:%04x", ev->vid, ev->pid);
    for (int i = 0; i < n; i++) {
        for (int j = 0; supported[i]->vidpids && supported[i]->vidpids[j]; j++) {
            if (strcmp(supported[i]->vidpids[j], want) == 0) {
                if (board_name_out) *board_name_out = supported[i]->name;
                if (bridge_str_out) *bridge_str_out = supported[i]->bridge;
                return;
            }
        }
    }
    if (board_name_out) *board_name_out = "(unknown)";
    if (bridge_str_out) *bridge_str_out = "?";
}

static void print_hotplug_event(const dvb_hotplug_event_t *ev) {
    const char *board_name = NULL;
    const char *bridge_str = NULL;
    event_lookup(ev, &board_name, &bridge_str);
    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", ev->vid, ev->pid);
    printf("  %c [%-7s] %s — USB %s @ bus %u dev %u\n",
           ev->kind == DVB_HOTPLUG_ARRIVED ? '+' : '-',
           bridge_str, board_name, vidpid,
           (unsigned)ev->bus, (unsigned)ev->devaddr);
    fflush(stdout);
}

static int watch_mode(void) {
    /* Print whatever's plugged in right now first, so the user sees
     * the initial state without having to wait for events. */
    (void)print_detected();
    printf("Watching for hotplug events (Ctrl-C to exit):\n");
    fflush(stdout);

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return 1;
    }
    fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
    fcntl(pipefd[1], F_SETFL, fcntl(pipefd[1], F_GETFL) | O_NONBLOCK);

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    int rc = dvb_hotplug_init(pipefd[1]);
    if (rc < 0) {
        if (rc == -ENOTSUP) {
            fprintf(stderr, "libusb build has no hotplug support; cannot watch.\n");
        } else {
            fprintf(stderr, "dvb_hotplug_init failed: %d\n", rc);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    while (!g_watch_stop) {
        struct pollfd p = { .fd = pipefd[0], .events = POLLIN };
        int pr = poll(&p, 1, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        /* Drain pipe bytes — content is irrelevant, just edge-trigger. */
        char drain[64];
        while (read(pipefd[0], drain, sizeof(drain)) > 0) { /* spin */ }

        dvb_hotplug_event_t ev;
        while (dvb_hotplug_pop(&ev) == 1) {
            print_hotplug_event(&ev);
        }
    }

    printf("\nStopping watch.\n");
    dvb_hotplug_shutdown();
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s                  # plugged-in devices (default)\n"
            "  %s --supported      # supported board table (no USB scan)\n"
            "  %s --watch          # initial scan, then live hotplug stream\n",
            argv0, argv0, argv0);
    return 2;
}

int main(int argc, char *argv[]) {
    if (argc == 1)                                    return print_detected();
    if (argc == 2 && !strcmp(argv[1], "--supported")) return print_supported();
    if (argc == 2 && !strcmp(argv[1], "--watch"))     return watch_mode();
    return usage(argv[0]);
}
