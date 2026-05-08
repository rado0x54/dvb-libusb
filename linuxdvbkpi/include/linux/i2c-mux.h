/* SPDX-License-Identifier: MIT */
/*
 * i2c-mux polyfill — minimal subset sufficient for upstream demods that
 * expose an i2c gate to the tuner (si2168). The mux exposes a virtual
 * `struct i2c_adapter` whose master_xfer wraps the parent adapter's
 * xfer with the demod's select/deselect callbacks. From the chip
 * driver's perspective, this is the same surface the kernel offers.
 *
 * Single-channel only: chip drivers that use this (si2168) allocate
 * one mux child for the tuner i2c bus. Multi-child support isn't
 * needed — fixes go in here only if a future lift requires it.
 */

#ifndef LINUXDVBKPI_LINUX_I2C_MUX_H
#define LINUXDVBKPI_LINUX_I2C_MUX_H

#include <linux/types.h>
#include <linux/i2c.h>

/* flags for i2c_mux_alloc — kept ABI-compatible with upstream values
 * (BIT(0)/BIT(1)/BIT(2)) so chip drivers passing them compile clean.
 * Behaviour: we ignore them — the mux is always a "gate" semantically. */
#define I2C_MUX_LOCKED      (1u << 0)
#define I2C_MUX_ARBITRATOR  (1u << 1)
#define I2C_MUX_GATE        (1u << 2)

struct i2c_mux_core {
    struct i2c_adapter *parent;
    struct device      *dev;
    void               *priv;

    int (*select)(struct i2c_mux_core *muxc, u32 chan_id);
    int (*deselect)(struct i2c_mux_core *muxc, u32 chan_id);

    int                 num_adapters;
    int                 max_adapters;
    /* Trailing flexible array per upstream's i2c_mux_core layout.
     * adapter[0] is the muxed child handed out by i2c_mux_add_adapter. */
    struct i2c_adapter *adapter[];
};

#ifdef __cplusplus
extern "C" {
#endif

struct i2c_mux_core *i2c_mux_alloc(struct i2c_adapter *parent,
                                   struct device *dev, int max_adapters,
                                   int sizeof_priv, u32 flags,
                                   int (*select)(struct i2c_mux_core *, u32),
                                   int (*deselect)(struct i2c_mux_core *, u32));

static inline void *i2c_mux_priv(struct i2c_mux_core *muxc) {
    return muxc ? muxc->priv : 0;
}

int  i2c_mux_add_adapter(struct i2c_mux_core *muxc,
                         u32 force_nr, u32 chan_id);
void i2c_mux_del_adapters(struct i2c_mux_core *muxc);

#ifdef __cplusplus
}
#endif

#endif /* LINUXDVBKPI_LINUX_I2C_MUX_H */
