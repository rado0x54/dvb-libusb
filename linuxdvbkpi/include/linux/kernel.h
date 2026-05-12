/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_KERNEL_H
#define LINUXDVBKPI_LINUX_KERNEL_H

#include <linux/types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef BIT
#define BIT(nr) ((u32)1 << (nr))
#endif

#ifndef BIT_ULL
#define BIT_ULL(nr) ((u64)1 << (nr))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#ifndef min
#define min(a, b) ({                       \
    __typeof__(a) _a = (a);                 \
    __typeof__(b) _b = (b);                 \
    _a < _b ? _a : _b;                      \
})
#endif

#ifndef max
#define max(a, b) ({                       \
    __typeof__(a) _a = (a);                 \
    __typeof__(b) _b = (b);                 \
    _a > _b ? _a : _b;                      \
})
#endif

#ifndef min_t
#define min_t(type, a, b) ({                \
    type _a = (type)(a);                    \
    type _b = (type)(b);                    \
    _a < _b ? _a : _b;                      \
})
#endif

#ifndef max_t
#define max_t(type, a, b) ({                \
    type _a = (type)(a);                    \
    type _b = (type)(b);                    \
    _a > _b ? _a : _b;                      \
})
#endif

#ifndef clamp
#define clamp(v, lo, hi) min(max(v, lo), hi)
#endif

#ifndef clamp_t
#define clamp_t(type, v, lo, hi) min_t(type, max_t(type, v, lo), hi)
#endif

/* clamp_val(v, lo, hi): same shape as clamp but computes the type
 * from v's typeof. Used by si2157 for clamping a strength reading. */
#ifndef clamp_val
#define clamp_val(v, lo, hi) clamp_t(__typeof__(v), v, lo, hi)
#endif

/* do_div(n, base): kernel macro — sets n = n / base, returns remainder.
 * Userspace does straight 64-bit division. The macro expands to a
 * statement-expression so call-site syntax (`do_div(x, y)`) works as
 * an expression yielding the remainder. */
#ifndef do_div
#define do_div(n, base) ({                              \
    u64 __base = (base);                                \
    u64 __rem  = (u64)(n) % __base;                     \
    (n) = (__typeof__(n))((u64)(n) / __base);           \
    __rem;                                              \
})
#endif

/* Integer rounding helpers used by chip drivers' IF/symbol-rate math. */
#define DIV_ROUND_UP(n, d)         (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(n, d)    (((n) + (d) / 2) / (d))
#define DIV_ROUND_CLOSEST_ULL(n, d) \
    ((u64)((u64)(n) + (u64)(d) / 2) / (u64)(d))

/* mn88472 uses MHz as a scale factor for frequency limits. */
#define MHz 1000000

/* upstream's `kzalloc_obj(*p)` => kzalloc(sizeof(*p), GFP_KERNEL).
 * (Introduced in 6.x; older drivers used the explicit form. We
 * provide both spellings.) */
#ifndef kzalloc_obj
#define kzalloc_obj(obj) calloc(1, sizeof(obj))
#endif

#ifndef ALIGN
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif

/* `fallthrough` — kernel macro for `switch` fallthrough markers,
 * normally `__attribute__((__fallthrough__))`. Some lifted chip
 * drivers (lgdt3306a) use it inside switch bodies. */
#ifndef fallthrough
#define fallthrough __attribute__((__fallthrough__))
#endif

/* unused()/might_sleep are kernel-side hints — no-op in userspace. */
#ifndef might_sleep
#define might_sleep() do { } while (0)
#endif

#ifndef WARN_ON
#define WARN_ON(cond) ({ int _c = !!(cond); _c; })
#endif

#ifndef WARN_ON_ONCE
#define WARN_ON_ONCE(cond) WARN_ON(cond)
#endif

/* Linux's printk priority macros — used by some `dev_*` legacy paths. */
#define KERN_EMERG    "<0>"
#define KERN_ALERT    "<1>"
#define KERN_CRIT     "<2>"
#define KERN_ERR      "<3>"
#define KERN_WARNING  "<4>"
#define KERN_NOTICE   "<5>"
#define KERN_INFO     "<6>"
#define KERN_DEBUG    "<7>"

#endif
