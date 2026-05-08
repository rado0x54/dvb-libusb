/* SPDX-License-Identifier: MIT */
/*
 * regmap polyfill — chip-driver subset.
 *
 * `regmap` in the kernel is a generic register-bus abstraction over
 * i2c/spi/mmio with caching, range tracking, and bus-format quirks.
 * Chip drivers only use it as "talk to my register space over i2c";
 * we provide that surface and ignore the rest (caching, range tables,
 * volatile-table, etc. are accepted in the config and discarded).
 *
 * Backed by direct i2c_master_send/recv to client->addr. Reg + val
 * widths are byte-quantized (8b reg, 8b val) — what mn88472, tda18250,
 * si2168 and friends use.
 */

#ifndef LINUXDVBKPI_LINUX_REGMAP_H
#define LINUXDVBKPI_LINUX_REGMAP_H

#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/err.h>

struct regmap;

struct regmap_range {
    unsigned int range_min;
    unsigned int range_max;
};

#define regmap_reg_range(_min, _max) { .range_min = (_min), .range_max = (_max) }

struct regmap_access_table {
    const struct regmap_range *yes_ranges;
    unsigned int               n_yes_ranges;
    const struct regmap_range *no_ranges;
    unsigned int               n_no_ranges;
};

struct regmap_config {
    int                                reg_bits;
    int                                val_bits;
    unsigned int                       max_register;
    const struct regmap_access_table  *volatile_table;
    const struct regmap_access_table  *readable_table;
    const struct regmap_access_table  *writeable_table;
    /* Other fields (cache_type, defaults, …) are accepted by chip
     * drivers but ignored here. */
};

#ifdef __cplusplus
extern "C" {
#endif

struct regmap *regmap_init_i2c(struct i2c_client *client,
                               const struct regmap_config *config);
struct regmap *devm_regmap_init_i2c(struct i2c_client *client,
                                    const struct regmap_config *config);
void regmap_exit(struct regmap *map);

int regmap_read       (struct regmap *map, unsigned int reg,
                       unsigned int *val);
int regmap_write      (struct regmap *map, unsigned int reg,
                       unsigned int val);
int regmap_bulk_read  (struct regmap *map, unsigned int reg,
                       void *val, size_t val_count);
int regmap_bulk_write (struct regmap *map, unsigned int reg,
                       const void *val, size_t val_count);
int regmap_update_bits(struct regmap *map, unsigned int reg,
                       unsigned int mask, unsigned int val);

#ifdef __cplusplus
}
#endif

/* `regmap_write_bits` is just an alias used in some drivers. */
#define regmap_write_bits(map, reg, mask, val) \
    regmap_update_bits((map), (reg), (mask), (val))

#endif
