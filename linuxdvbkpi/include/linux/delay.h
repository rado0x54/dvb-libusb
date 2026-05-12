/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_DELAY_H
#define LINUXDVBKPI_LINUX_DELAY_H

#include <linux/types.h>

#include <time.h>

static inline void msleep(unsigned int ms) {
    if (!ms) return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000u),
        .tv_nsec = (long)((ms % 1000u) * 1000000UL),
    };
    nanosleep(&ts, NULL);
}

static inline void udelay(unsigned int us) {
    struct timespec ts = {
        .tv_sec  = (time_t)(us / 1000000u),
        .tv_nsec = (long)((us % 1000000u) * 1000UL),
    };
    nanosleep(&ts, NULL);
}

static inline void usleep_range(unsigned long min_us, unsigned long max_us) {
    /* The kernel sleeps for somewhere in [min, max] to give the
     * scheduler a coalescing window. We just sleep for the average,
     * which is close enough for chip-init timing. */
    unsigned long us = (min_us + max_us) / 2u;
    udelay((unsigned int)us);
}

#endif
