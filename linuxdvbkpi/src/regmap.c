/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * regmap polyfill — narrow chip-driver subset.
 *
 * The kernel's `regmap` is a generic register-bus abstraction over
 * i2c/spi/mmio with caching, range tracking, and bus-format
 * translation. Chip drivers we lift use it only as "read/write a
 * register over the underlying i2c bus", so we provide that and
 * nothing more.
 *
 * Wire format: 8-bit register address, 8-bit data — what every chip
 * driver in our scope (mn88472, tda18250, si2168, si2157, …) uses.
 * The `reg_bits` / `val_bits` config fields are accepted but only
 * the (8, 8) shape is actually implemented; anything else returns
 * an error from regmap_init_i2c.
 *
 * No caching, no range tracking, no volatile-table — drivers express
 * those as hints to the kernel and we just re-issue the i2c xfer
 * each time. That's exactly the behaviour we want in userspace: every
 * read/write goes to the chip, never a stale cached value.
 */

#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/types.h>

#include <errno.h>
#include <string.h>

struct regmap {
    struct i2c_client *client;
    int                reg_bits;
    int                val_bits;
};

struct regmap *regmap_init_i2c(struct i2c_client *client,
                               const struct regmap_config *config) {
    if (!client || !config) return ERR_PTR(-EINVAL);
    if (config->reg_bits != 8 || config->val_bits != 8) {
        /* Polyfill scope: only 8/8 chip drivers. Grow later if a
         * 16-bit driver lands. */
        return ERR_PTR(-EOPNOTSUPP);
    }
    struct regmap *m = calloc(1, sizeof(*m));
    if (!m) return ERR_PTR(-ENOMEM);
    m->client   = client;
    m->reg_bits = config->reg_bits;
    m->val_bits = config->val_bits;
    return m;
}

struct regmap *devm_regmap_init_i2c(struct i2c_client *client,
                                    const struct regmap_config *config) {
    /* devm in the kernel ties lifetime to the device — in userspace we
     * leak one regmap per chip, freed implicitly at process exit. The
     * chip-driver remove path is rarely exercised in our flow, so this
     * is fine. (If we ever care, we can hang the regmap off the
     * client and free in i2c_unregister_device.) */
    return regmap_init_i2c(client, config);
}

void regmap_exit(struct regmap *map) {
    free(map);
}

int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val) {
    if (!map || !val) return -EINVAL;
    u8 wbuf = (u8)reg;
    u8 rbuf = 0;
    struct i2c_msg msgs[2] = {
        { .addr = map->client->addr, .flags = 0,        .len = 1, .buf = &wbuf },
        { .addr = map->client->addr, .flags = I2C_M_RD, .len = 1, .buf = &rbuf },
    };
    int ret = i2c_transfer(map->client->adapter, msgs, 2);
    if (ret < 0) return ret;
    if (ret != 2) return -EIO;
    *val = rbuf;
    return 0;
}

int regmap_write(struct regmap *map, unsigned int reg, unsigned int val) {
    if (!map) return -EINVAL;
    u8 buf[2] = { (u8)reg, (u8)val };
    struct i2c_msg msg = {
        .addr = map->client->addr, .flags = 0, .len = 2, .buf = buf,
    };
    int ret = i2c_transfer(map->client->adapter, &msg, 1);
    if (ret < 0) return ret;
    return ret == 1 ? 0 : -EIO;
}

int regmap_bulk_read(struct regmap *map, unsigned int reg,
                     void *val, size_t count) {
    if (!map || !val) return -EINVAL;
    u8 wbuf = (u8)reg;
    struct i2c_msg msgs[2] = {
        { .addr = map->client->addr, .flags = 0,        .len = 1,        .buf = &wbuf },
        { .addr = map->client->addr, .flags = I2C_M_RD, .len = (u16)count, .buf = val },
    };
    int ret = i2c_transfer(map->client->adapter, msgs, 2);
    if (ret < 0) return ret;
    return ret == 2 ? 0 : -EIO;
}

int regmap_bulk_write(struct regmap *map, unsigned int reg,
                      const void *val, size_t count) {
    if (!map || !val) return -EINVAL;
    /* reg + payload — single contiguous write. Cap at a sane size;
     * upstream's mn88472 already chunks via dev->i2c_write_max. */
    if (count > 64) {
        /* The bridge's i2c hardware caps single-shot writes; chip
         * drivers respect their `i2c_wr_max` field, so anything > 64
         * here is a logic bug above us. Refuse rather than truncate. */
        return -EOPNOTSUPP;
    }
    u8 buf[1 + 64];
    buf[0] = (u8)reg;
    memcpy(buf + 1, val, count);
    struct i2c_msg msg = {
        .addr = map->client->addr, .flags = 0,
        .len  = (u16)(1 + count), .buf = buf,
    };
    int ret = i2c_transfer(map->client->adapter, &msg, 1);
    if (ret < 0) return ret;
    return ret == 1 ? 0 : -EIO;
}

int regmap_update_bits(struct regmap *map, unsigned int reg,
                       unsigned int mask, unsigned int val) {
    unsigned int cur;
    int ret = regmap_read(map, reg, &cur);
    if (ret < 0) return ret;
    unsigned int new = (cur & ~mask) | (val & mask);
    if (new == cur) return 0;
    return regmap_write(map, reg, new);
}
