/* SPDX-License-Identifier: MIT */
/*
 * dvb_devices — list DVB hardware this build knows about and/or
 * has plugged in. No tuning, no streaming.
 *
 *   usage:
 *     dvb_devices                         # default: --supported
 *     dvb_devices --supported             # static board tables (no USB)
 *     dvb_devices --detected <fw_dir>     # opens every plugged-in device
 *     dvb_devices --all <fw_dir>          # both lists
 *
 * `--supported` shows what the build knows how to drive, regardless
 * of what's plugged in (one row per board, with all VID:PIDs).
 *
 * `--detected` actually scans USB, brings up bridges + chip drivers
 * for everything found, and lists each frontend the engines
 * publish. Needs $FIRMWARE_DIR pointing at the firmware blobs.
 */

#include "dvb_handle/dvb_handle.h"
#include "dvb_em28xx/dvb_em28xx.h"
#include "dvb_dib0700/dvb_dib0700.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void linuxdvbkpi_set_firmware_root(const char *path);

#define MAX_HANDLES 8

static int usage(const char *argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s                          # list supported (default)\n"
            "  %s --supported\n"
            "  %s --detected <fw_dir>\n"
            "  %s --all <fw_dir>\n",
            argv0, argv0, argv0, argv0);
    return 2;
}

static void print_supported(void) {
    const dvb_supported_board_t *em_list, *dib_list;
    int em_n = 0, dib_n = 0;
    em_list  = dvb_em28xx_supported_boards (&em_n);
    dib_list = dvb_dib0700_supported_boards(&dib_n);

    printf("Supported devices (%d board record%s):\n",
           em_n + dib_n, (em_n + dib_n) == 1 ? "" : "s");

    const dvb_supported_board_t *lists[]  = { em_list, dib_list };
    int                          counts[] = { em_n,    dib_n    };
    for (int g = 0; g < 2; g++) {
        for (int i = 0; i < counts[g]; i++) {
            const dvb_supported_board_t *b = &lists[g][i];
            printf("  [%s] %s — %d frontend%s\n",
                   b->bridge, b->name, b->num_frontends,
                   b->num_frontends == 1 ? "" : "s");
            for (int j = 0; b->vidpids && b->vidpids[j]; j++) {
                printf("    USB %s\n", b->vidpids[j]);
            }
        }
    }
}

static int print_detected(const char *fw_dir) {
    linuxdvbkpi_set_firmware_root(fw_dir);
    setenv("FIRMWARE_DIR", fw_dir, /*overwrite=*/1);

    dvb_frontend_handle_t *handles[MAX_HANDLES] = {0};
    int n = 0;
    n += dvb_em28xx_discover_all (&handles[n], MAX_HANDLES - n);
    n += dvb_dib0700_discover_all(&handles[n], MAX_HANDLES - n);

    printf("Detected (plugged in, %d frontend%s):\n",
           n, n == 1 ? "" : "s");
    for (int i = 0; i < n; i++) {
        printf("  [%d] %s\n", i, handles[i]->display_name);
    }
    if (n == 0) {
        printf("  (none — check that supported hardware is plugged in)\n");
    }

    dvb_dib0700_shutdown();
    dvb_em28xx_shutdown();
    return n > 0 ? 0 : 1;
}

int main(int argc, char *argv[]) {
    /* No args = --supported. Lets the user just type `dvb_devices`
     * to see what the build knows. */
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--supported"))) {
        print_supported();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--detected")) {
        return print_detected(argv[2]);
    }
    if (argc == 3 && !strcmp(argv[1], "--all")) {
        print_supported();
        printf("\n");
        return print_detected(argv[2]);
    }
    return usage(argv[0]);
}
