/* SPDX-License-Identifier: MIT */
/*
 * <asm/div64.h> polyfill — upstream chip drivers (lgdt3306a) include
 * this for `do_div`, which our `<linux/kernel.h>` already polyfills.
 * Empty stub so the include resolves; the actual macro lives in
 * kernel.h.
 */

#ifndef LINUXDVBKPI_ASM_DIV64_H
#define LINUXDVBKPI_ASM_DIV64_H

#include <linux/kernel.h>

#endif /* LINUXDVBKPI_ASM_DIV64_H */
