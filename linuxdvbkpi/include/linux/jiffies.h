/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_JIFFIES_H
#define LINUXDVBKPI_LINUX_JIFFIES_H

#include <linux/types.h>

#include <time.h>

/* The kernel's `jiffies` is a monotonic counter that ticks at HZ.
 * We expose `jiffies` as a millisecond-since-some-epoch value (HZ=1000
 * effectively); msecs_to_jiffies/jiffies_to_msecs are 1:1. The chip
 * drivers compare with `time_after(jiffies, deadline)`, which works as
 * long as we're consistent about units. */

#define HZ 1000UL

static inline unsigned long linuxdvbkpi_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000UL +
           (unsigned long)(ts.tv_nsec / 1000000UL);
}

#define jiffies (linuxdvbkpi_now_ms())

static inline unsigned long msecs_to_jiffies(unsigned int ms) {
    return ms;
}

static inline unsigned int jiffies_to_msecs(unsigned long j) {
    return (unsigned int)j;
}

#define time_after(a, b)  ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)
#define time_after_eq(a, b)  ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) time_after_eq(b, a)

#endif
