/* SPDX-License-Identifier: MIT */
#ifndef LINUXDVBKPI_LINUX_SLAB_H
#define LINUXDVBKPI_LINUX_SLAB_H

#include <linux/types.h>

#include <stdlib.h>
#include <string.h>

/* Kernel GFP_* flags — all map to "regular allocation" in userland. */
#define GFP_KERNEL  0
#define GFP_ATOMIC  0
#define GFP_NOWAIT  0
#define GFP_USER    0
#define GFP_DMA     0

#define __GFP_ZERO  0

static inline void *kmalloc(size_t size, int flags) {
    (void)flags;
    return malloc(size);
}

static inline void *kzalloc(size_t size, int flags) {
    (void)flags;
    return calloc(1, size);
}

static inline void *kcalloc(size_t n, size_t size, int flags) {
    (void)flags;
    return calloc(n, size);
}

static inline void *krealloc(void *p, size_t size, int flags) {
    (void)flags;
    return realloc(p, size);
}

static inline void *kmemdup(const void *src, size_t size, int flags) {
    (void)flags;
    void *p = malloc(size);
    if (p) memcpy(p, src, size);
    return p;
}

static inline void kfree(const void *p) {
    free((void *)p);
}

/* kzalloc_obj is defined in <linux/kernel.h>. */

#endif
