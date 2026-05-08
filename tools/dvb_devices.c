/* SPDX-License-Identifier: MIT */
/*
 * dvb_devices — list DVB hardware this build knows about and/or
 * has plugged in.
 *
 * Default mode (no args): show every supported board the build can
 * drive, marking which are currently plugged in. Pure USB-bus
 * enumeration; no firmware required, no device claim, no chip
 * bring-up.
 *
 *   dvb_devices                # default: supported list, plus
 *                              #   "[connected]" mark per row
 *   dvb_devices --supported    # supported list only (no USB scan)
 *   dvb_devices --detected     # only currently plugged-in devices
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

/* Collect every supported board across both engines into one flat
 * array. Pointer-stable for the process lifetime (engines cache). */
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

/* Return the VID:PID of the first variant of `sup` that's currently
 * plugged in, or NULL if none of the variants is. */
static const char *find_present_vidpid(const dvb_supported_board_t *sup,
                                       const dvb_present_board_t *present,
                                       int present_count) {
    if (!sup->vidpids) return NULL;
    for (int i = 0; sup->vidpids[i]; i++) {
        for (int j = 0; j < present_count; j++) {
            if (strcmp(sup->vidpids[i], present[j].vidpid) == 0) {
                return present[j].vidpid;
            }
        }
    }
    return NULL;
}

static int print_combined(void) {
    const dvb_supported_board_t *supported[MAX_BOARDS] = {0};
    int sup_count = gather_supported(supported, MAX_BOARDS);

    dvb_present_board_t present[MAX_BOARDS] = {0};
    int present_count = gather_present(present, MAX_BOARDS);

    int connected = 0;
    for (int i = 0; i < sup_count; i++) {
        if (find_present_vidpid(supported[i], present, present_count)) connected++;
    }

    printf("Supported devices (%d, %d connected):\n", sup_count, connected);
    for (int i = 0; i < sup_count; i++) {
        const dvb_supported_board_t *b = supported[i];
        const char *here = find_present_vidpid(b, present, present_count);
        printf("  %s [%-7s] %s — %d frontend%s\n",
               here ? "[CONNECTED]" : "[          ]",
               b->bridge, b->name, b->num_frontends,
               b->num_frontends == 1 ? "" : "s");
        if (here) {
            printf("              USB %s\n", here);
        } else {
            for (int j = 0; b->vidpids && b->vidpids[j]; j++) {
                printf("              USB %s\n", b->vidpids[j]);
            }
        }
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

static int print_detected(void) {
    dvb_present_board_t present[MAX_BOARDS] = {0};
    int n = gather_present(present, MAX_BOARDS);
    printf("Detected devices (%d connected):\n", n);
    if (n == 0) {
        printf("  (no supported hardware plugged in)\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        printf("  [%-7s] %s — USB %s, %d frontend%s\n",
               present[i].bridge, present[i].name, present[i].vidpid,
               present[i].num_frontends,
               present[i].num_frontends == 1 ? "" : "s");
    }
    return 0;
}

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s                  # supported boards + which are connected (default)\n"
            "  %s --supported      # supported boards only (no USB scan)\n"
            "  %s --detected       # currently plugged-in boards only\n",
            argv0, argv0, argv0);
    return 2;
}

int main(int argc, char *argv[]) {
    if (argc == 1)                                    return print_combined();
    if (argc == 2 && !strcmp(argv[1], "--supported")) return print_supported();
    if (argc == 2 && !strcmp(argv[1], "--detected"))  return print_detected();
    return usage(argv[0]);
}
