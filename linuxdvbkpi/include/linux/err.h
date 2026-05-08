/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_ERR_H
#define LINUXDVBKPI_LINUX_ERR_H

#include <linux/types.h>
#include <errno.h>
#include <stdint.h>

/* Linux uses pointers in the top 4 KiB to encode error codes; we do
 * the same so `IS_ERR(p) ? PTR_ERR(p) : 0` reads identically. We
 * cap to the same MAX_ERRNO value (4095). */
#define MAX_ERRNO 4095

static inline int IS_ERR(const void *p) {
    return (uintptr_t)p >= (uintptr_t)-MAX_ERRNO;
}

static inline long PTR_ERR(const void *p) {
    return (long)(intptr_t)p;
}

static inline void *ERR_PTR(long error) {
    return (void *)(intptr_t)error;
}

static inline int IS_ERR_OR_NULL(const void *p) {
    return !p || IS_ERR(p);
}

static inline void *ERR_CAST(const void *p) {
    return (void *)p;
}

#endif
