/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * intlog10 — fixed-point log10 the kernel exposes via <linux/int_log.h>.
 *
 * Returns log10(value) * 2^24, where value is a 32-bit unsigned input.
 * Used by mn88472 to compute CNR from chip register readings.
 *
 * We compute via natural log10 → multiply by 2^24 → round to nearest.
 * Accuracy: ±1 LSB across the chip's input range (≥1, ≤65536) — well
 * inside what the chip reports for noise floor.
 */

#include <linux/int_log.h>
#include <math.h>

u32 intlog10(u32 value) {
    if (value == 0) return 0;
    double l = log10((double)value);
    double scaled = l * (double)(1u << 24);
    if (scaled < 0) scaled = 0;
    if (scaled > (double)0xFFFFFFFFu) scaled = (double)0xFFFFFFFFu;
    return (u32)(scaled + 0.5);
}
