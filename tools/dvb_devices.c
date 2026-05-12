/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dvb_devices — list DVB hardware that's plugged in (default), or
 * the build's full board table (--supported).
 *
 * Default mode (no args): scan USB and print one entry per physical
 * device that matches a board this build knows. Two of the same
 * board show up as two entries (different bus/address). Pure
 * libusb enumeration — no firmware required, no device claim, no
 * chip bring-up.
 *
 *   dvb_devices                # plugged-in devices (default)
 *   dvb_devices --supported    # supported board table (no USB scan)
 *
 * Firmware: this tool never opens a device, so firmware is
 * irrelevant here. It matters for `dvb_pid_dump`. The library's
 * lookup chain is `linuxdvbkpi_set_firmware_root()` → $FIRMWARE_DIR
 * → /usr/local/lib/firmware → /usr/lib/firmware → /lib/firmware.
 */

#include "dvb_handle/dvb_handle.h"
#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_dib0700/dvb_dib0700.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BOARDS 32

static int gather_supported(const dvb_supported_board_t *out[], int max) {
    int n = 0;
    int em_n = 0, dib_n = 0;
    const dvb_supported_board_t *em  = dvb_em28xx_supported_boards (&em_n);
    const dvb_supported_board_t *dib = dvb_dib0700_supported_boards(&dib_n);
    for (int i = 0; i < em_n  && n < max; i++) out[n++] = &em [i];
    for (int i = 0; i < dib_n && n < max; i++) out[n++] = &dib[i];
    return n;
}

static int gather_present(dvb_present_board_t *out, int max) {
    int n = 0;
    n += dvb_em28xx_scan_present (&out[n], max - n);
    n += dvb_dib0700_scan_present(&out[n], max - n);
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

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s                  # plugged-in devices (default)\n"
            "  %s --supported      # supported board table (no USB scan)\n",
            argv0, argv0);
    return 2;
}

int main(int argc, char *argv[]) {
    if (argc == 1)                                    return print_detected();
    if (argc == 2 && !strcmp(argv[1], "--supported")) return print_supported();
    return usage(argv[0]);
}
