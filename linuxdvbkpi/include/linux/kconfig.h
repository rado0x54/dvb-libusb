/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kernel `<linux/kconfig.h>` exposes IS_ENABLED(CONFIG_FOO) — a
 * compile-time check on a CONFIG_ symbol's value. We expose the same
 * macro shape; chip drivers use it for optional code paths (e.g.
 * media-controller integration), all of which we leave disabled. */
#ifndef LINUXDVBKPI_LINUX_KCONFIG_H
#define LINUXDVBKPI_LINUX_KCONFIG_H

#define IS_ENABLED(option) (0)
#define IS_BUILTIN(option) (0)
#define IS_MODULE(option)  (0)

/* IS_REACHABLE: whether the named driver/symbol is built in. We link
 * every chip driver via link_whole, so it's always reachable from
 * its own header's perspective — this prevents lifted .h files from
 * emitting `static inline` no-op fallbacks that collide with the
 * real .c definitions (e.g. lgdt3306a.h's lgdt3306a_attach stub). */
#define IS_REACHABLE(option) (1)

#endif
