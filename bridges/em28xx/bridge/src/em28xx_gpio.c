/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * em28xx bridge-mode + GPIO-sequence application.
 *
 * Direct port of em28xx_gpio_set from
 * drivers/media/usb/em28xx/em28xx-core.c
 * (torvalds/linux @ 9207d47f).
 *
 * Each board entry in upstream's em28xx_boards[] table carries up
 * to four reg_seq pointers (analog, dvb, suspend, tuner_gpio) that
 * are applied at well-defined points in the device lifecycle. The
 * sequences pulse demod-reset GPIOs, enable LED bits, set TS-bus
 * packet sizes, etc. — everything bridge-side that has to happen
 * before the demod / tuner can be initialised properly.
 *
 * In our flat userland API the orchestration moves up a layer:
 * the engine (or, for now, the bring-up probe) decides when to
 * apply which sequence. This file just runs the sequence given
 * to it.
 */

#include "em28xx_priv.h"

#include <errno.h>
#include <time.h>

/* Upstream's prelude writes a magic-numbered register (0x48) and
 * EM28XX_R12_VINENABLE depending on the target mode. There's no
 * symbolic name for 0x48 in em28xx-reg.h either upstream or here. */
#define EM28XX_R48_UNKNOWN_PRELUDE 0x48
#define EM28XX_VINENABLE_DIGITAL   0x37
#define EM28XX_VINENABLE_ANALOG    0x67

static void em28xx_gpio_msleep(unsigned ms) {
    if (!ms) return;
    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000u),
        .tv_nsec = (long)((ms % 1000u) * 1000000u),
    };
    nanosleep(&ts, NULL);
}

int em28xx_set_xclk(em28xx_dev_t *dev, uint8_t xclk) {
    if (!dev) {
        return -EINVAL;
    }
    return em28xx_write_reg(dev, EM28XX_R0F_XCLK, xclk);
}

int em28xx_gpio_set(em28xx_dev_t *dev, enum em28xx_mode mode,
                    const em28xx_reg_seq_t *seq) {
    if (!dev) {
        return -EINVAL;
    }

    int ret = 0;

    /* Prelude — only when transitioning OUT of suspend. Mirrors
     * the upstream `if (dev->mode != EM28XX_SUSPEND) { ... }` guard.
     * The 10 ms settle after VINENABLE is upstream's
     * `usleep_range(10000, 11000)`. */
    if (mode != EM28XX_MODE_SUSPEND) {
        ret = em28xx_write_reg(dev, EM28XX_R48_UNKNOWN_PRELUDE, 0x00);
        if (ret < 0) {
            return ret;
        }
        uint8_t vinenable = (mode == EM28XX_MODE_ANALOG)
            ? EM28XX_VINENABLE_ANALOG
            : EM28XX_VINENABLE_DIGITAL;
        ret = em28xx_write_reg(dev, EM28XX_R12_VINENABLE, vinenable);
        if (ret < 0) {
            return ret;
        }
        em28xx_gpio_msleep(10);
    }

    if (!seq) {
        return 0;
    }

    /* Walk the sequence. Same termination + skip rules as upstream:
     *   sleep_ms < 0 → end of array
     *   reg < 0      → no register write, just sleep */
    while (seq->sleep_ms >= 0) {
        if (seq->reg >= 0) {
            ret = em28xx_write_reg_bits(dev,
                                        (uint16_t)seq->reg,
                                        (uint8_t)seq->val,
                                        (uint8_t)seq->mask);
            if (ret < 0) {
                return ret;
            }
        }
        if (seq->sleep_ms > 0) {
            em28xx_gpio_msleep((unsigned)seq->sleep_ms);
        }
        seq++;
    }
    return 0;
}
