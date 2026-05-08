/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * v4l2-mc polyfill — covers two upstream things our chip drivers
 * touch by including this header:
 *
 *   1. Media-controller / v4l2 graph entities. All actual uses are
 *      gated behind `#if defined(CONFIG_MEDIA_CONTROLLER)` which
 *      we leave undefined; nothing else is needed here.
 *
 *   2. V4L2_STD_* analog TV standard bitmask constants. Upstream
 *      tuner drivers (si2157) reference them inside set_analog_params,
 *      which we never call — but the source must compile. The values
 *      are 1:1 with upstream's <uapi/linux/videodev2.h>; we copy
 *      only what the lifted drivers reference.
 */

#ifndef LINUXDVBKPI_MEDIA_V4L2_MC_H
#define LINUXDVBKPI_MEDIA_V4L2_MC_H

#include <linux/types.h>

typedef u64 v4l2_std_id;

#define V4L2_STD_PAL_B          ((v4l2_std_id)0x00000001)
#define V4L2_STD_PAL_B1         ((v4l2_std_id)0x00000002)
#define V4L2_STD_PAL_G          ((v4l2_std_id)0x00000004)
#define V4L2_STD_PAL_H          ((v4l2_std_id)0x00000008)
#define V4L2_STD_PAL_I          ((v4l2_std_id)0x00000010)
#define V4L2_STD_PAL_D          ((v4l2_std_id)0x00000020)
#define V4L2_STD_PAL_D1         ((v4l2_std_id)0x00000040)
#define V4L2_STD_PAL_K          ((v4l2_std_id)0x00000080)
#define V4L2_STD_PAL_M          ((v4l2_std_id)0x00000100)
#define V4L2_STD_PAL_N          ((v4l2_std_id)0x00000200)
#define V4L2_STD_PAL_Nc         ((v4l2_std_id)0x00000400)
#define V4L2_STD_PAL_60         ((v4l2_std_id)0x00000800)
#define V4L2_STD_NTSC_M         ((v4l2_std_id)0x00001000)
#define V4L2_STD_NTSC_M_JP      ((v4l2_std_id)0x00002000)
#define V4L2_STD_NTSC_443       ((v4l2_std_id)0x00004000)
#define V4L2_STD_NTSC_M_KR      ((v4l2_std_id)0x00008000)
#define V4L2_STD_SECAM_B        ((v4l2_std_id)0x00010000)
#define V4L2_STD_SECAM_D        ((v4l2_std_id)0x00020000)
#define V4L2_STD_SECAM_G        ((v4l2_std_id)0x00040000)
#define V4L2_STD_SECAM_H        ((v4l2_std_id)0x00080000)
#define V4L2_STD_SECAM_K        ((v4l2_std_id)0x00100000)
#define V4L2_STD_SECAM_K1       ((v4l2_std_id)0x00200000)
#define V4L2_STD_SECAM_L        ((v4l2_std_id)0x00400000)
#define V4L2_STD_SECAM_LC       ((v4l2_std_id)0x00800000)

#define V4L2_STD_PAL_BG         (V4L2_STD_PAL_B | V4L2_STD_PAL_B1 | V4L2_STD_PAL_G)
#define V4L2_STD_PAL_DK         (V4L2_STD_PAL_D | V4L2_STD_PAL_D1 | V4L2_STD_PAL_K)
#define V4L2_STD_SECAM_DK       (V4L2_STD_SECAM_D | V4L2_STD_SECAM_K | V4L2_STD_SECAM_K1)
#define V4L2_STD_DK             (V4L2_STD_PAL_DK  | V4L2_STD_SECAM_DK)
#define V4L2_STD_B              (V4L2_STD_PAL_B   | V4L2_STD_PAL_B1 | V4L2_STD_SECAM_B)
#define V4L2_STD_G              (V4L2_STD_PAL_G   | V4L2_STD_SECAM_G)
#define V4L2_STD_H              (V4L2_STD_PAL_H   | V4L2_STD_SECAM_H)
#define V4L2_STD_GH             (V4L2_STD_G       | V4L2_STD_H)
#define V4L2_STD_MN             (V4L2_STD_PAL_M   | V4L2_STD_PAL_N | V4L2_STD_PAL_Nc | V4L2_STD_NTSC)
#define V4L2_STD_NTSC           (V4L2_STD_NTSC_M  | V4L2_STD_NTSC_M_JP | V4L2_STD_NTSC_M_KR)

/* v4l2 tuner mode (analog vs. radio). Used as a sentinel; values
 * follow upstream's enum v4l2_tuner_type. */
#define V4L2_TUNER_RADIO        1
#define V4L2_TUNER_ANALOG_TV    2
#define V4L2_TUNER_DIGITAL_TV   3

#endif /* LINUXDVBKPI_MEDIA_V4L2_MC_H */
