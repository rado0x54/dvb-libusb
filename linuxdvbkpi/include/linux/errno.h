/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Polyfill for <linux/errno.h>.
 *
 * On Linux a system <linux/errno.h> exists (kernel UAPI) — and glibc's
 * <errno.h> chains through it via <bits/errno.h> to populate
 * asm-generic/errno-base.h (EIO/EINVAL/E2BIG/...). If our polyfill
 * shadows the system header without chaining onward, that chain hits
 * our (empty under header guard) polyfill on recursion and the kernel
 * errno macros never get defined. So we #include_next to reach the
 * real kernel header when present; on macOS there's no such header
 * and #include_next is skipped via __has_include_next.
 *
 * Then we pull in libc errno.h for the POSIX superset that chip
 * drivers also touch via plain `EIO` etc., and add the few kernel-
 * specific errnos that aren't in either source. */
#ifndef LINUXDVBKPI_LINUX_ERRNO_H
#define LINUXDVBKPI_LINUX_ERRNO_H

#if defined(__has_include_next)
#  if __has_include_next(<linux/errno.h>)
#    include_next <linux/errno.h>
#  endif
#endif

#include <errno.h>

/* Linux kernel uses these in addition to POSIX. Map onto closest POSIX
 * errno so chip-driver source compiles unchanged. */
#ifndef EREMOTEIO
#define EREMOTEIO EIO
#endif
#ifndef ERESTARTSYS
#define ERESTARTSYS EINTR
#endif

#endif
