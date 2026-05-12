/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_PRINTK_H
#define LINUXDVBKPI_LINUX_PRINTK_H

#include <stdio.h>

/* Compile-time switch: define LINUXDVBKPI_VERBOSE=1 to route dev_dbg
 * to stderr; otherwise it's elided. dev_err / dev_info / dev_warn
 * always print. */
#ifndef LINUXDVBKPI_VERBOSE
#define LINUXDVBKPI_VERBOSE 0
#endif

#define pr_emerg(fmt, ...)   fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)   fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)    fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)     fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_warning(fmt, ...) fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)    fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)  fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)    fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)

#if LINUXDVBKPI_VERBOSE
#define pr_debug(fmt, ...)   fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)   ((void)0)
#endif

/* printk — kernel's raw logger. Some lifted chip drivers (lgdt3306a)
 * call it directly with a KERN_* level prefix string-concatenated
 * onto the format. We don't bother stripping the level; the leading
 * "<N>" just shows up in the log line. */
#define printk(fmt, ...)     fprintf(stderr, "[lkpi] " fmt, ##__VA_ARGS__)

/* KBUILD_MODNAME — normally set per-TU by the kernel's build system
 * via -DKBUILD_MODNAME="...". Lifted drivers that use it transitively
 * (typically through `pr_fmt`) need a default. Per-driver meson stanzas
 * can override with their own -DKBUILD_MODNAME. */
#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "linuxdvbkpi"
#endif

#endif
