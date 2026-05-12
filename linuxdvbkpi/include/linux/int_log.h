/* SPDX-License-Identifier: GPL-2.0-or-later */
/* mn88472 uses intlog10 to compute CNR from chip register values.
 * Returns log10(value) * 2^24 (Q24 fixed-point), matching upstream's
 * <linux/int_log.h> intlog10 contract. */
#ifndef LINUXDVBKPI_LINUX_INT_LOG_H
#define LINUXDVBKPI_LINUX_INT_LOG_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 intlog10(u32 value);

#ifdef __cplusplus
}
#endif

#endif
