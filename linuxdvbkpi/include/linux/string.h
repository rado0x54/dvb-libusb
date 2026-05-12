/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_STRING_H
#define LINUXDVBKPI_LINUX_STRING_H

#include_next <string.h>

/* `strscpy` was added to the kernel in 4.3 and made public to drivers
 * shortly after; chip drivers copy strings into i2c_board_info.type
 * with it. POSIX strlcpy isn't quite the same (it returns total length
 * not bytes-copied) so we provide a small wrapper. Returns the number
 * of bytes copied, or -E2BIG on truncation, matching the kernel. */
#include <stddef.h>
/* libc errno.h directly — on Linux, the system's <linux/errno.h> is
 * the kernel UAPI header which doesn't include the libc errno values
 * E2BIG/EIO/etc.; pulling errno.h first guarantees those macros. */
#include <errno.h>
#include <linux/errno.h>

static inline ssize_t strscpy(char *dst, const char *src, size_t size) {
    if (size == 0) return -E2BIG;
    size_t i;
    for (i = 0; i + 1 < size && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return src[i] ? -E2BIG : (ssize_t)i;
}

#endif
