/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * i2c-mux polyfill implementation. The mux's child adapter forwards
 * each i2c transfer through the parent adapter, calling the chip
 * driver's select/deselect callbacks around it. That's all the
 * kernel's mux core actually does for our consumers (si2168).
 */

#include <linux/i2c-mux.h>

#include <stdlib.h>
#include <string.h>

static int mux_master_xfer(struct i2c_adapter *adap,
                           struct i2c_msg *msgs, int num) {
    struct i2c_mux_core *muxc = adap->algo_data;
    if (!muxc || !muxc->parent) return -EIO;

    int rc;
    if (muxc->select) {
        rc = muxc->select(muxc, 0);
        if (rc < 0) return rc;
    }
    int xfer_rc = i2c_transfer(muxc->parent, msgs, num);
    int des_rc  = 0;
    if (muxc->deselect) {
        des_rc = muxc->deselect(muxc, 0);
    }
    if (xfer_rc < 0) return xfer_rc;
    if (des_rc  < 0) return des_rc;
    return xfer_rc;
}

static u32 mux_functionality(struct i2c_adapter *adap) {
    struct i2c_mux_core *muxc = adap->algo_data;
    if (!muxc || !muxc->parent || !muxc->parent->algo
        || !muxc->parent->algo->functionality) {
        return I2C_FUNC_I2C;
    }
    return muxc->parent->algo->functionality(muxc->parent);
}

static const struct i2c_algorithm mux_algo = {
    .master_xfer   = mux_master_xfer,
    .functionality = mux_functionality,
};

struct i2c_mux_core *i2c_mux_alloc(struct i2c_adapter *parent,
                                   struct device *dev, int max_adapters,
                                   int sizeof_priv, u32 flags,
                                   int (*select)(struct i2c_mux_core *, u32),
                                   int (*deselect)(struct i2c_mux_core *, u32)) {
    (void)flags;
    (void)sizeof_priv;
    if (!parent || max_adapters < 1) return NULL;

    /* Allocate the core + one i2c_adapter slot up front. We only ever
     * grow up to max_adapters via i2c_mux_add_adapter; one slot is
     * sufficient for everything the polyfill targets today. */
    size_t total = sizeof(struct i2c_mux_core)
                   + (size_t)max_adapters * sizeof(struct i2c_adapter *)
                   + (size_t)max_adapters * sizeof(struct i2c_adapter);
    struct i2c_mux_core *muxc = calloc(1, total);
    if (!muxc) return NULL;

    muxc->parent       = parent;
    muxc->dev          = dev;
    muxc->select       = select;
    muxc->deselect     = deselect;
    muxc->num_adapters = 0;
    muxc->max_adapters = max_adapters;
    return muxc;
}

int i2c_mux_add_adapter(struct i2c_mux_core *muxc,
                        u32 force_nr, u32 chan_id) {
    (void)force_nr;
    (void)chan_id;
    if (!muxc) return -EINVAL;
    if (muxc->num_adapters >= muxc->max_adapters) return -ENOSPC;

    /* The flexible adapter[] array stores pointers; the actual
     * i2c_adapter structs live in the allocation tail. */
    struct i2c_adapter *child_storage =
        (struct i2c_adapter *)((char *)muxc
            + sizeof(struct i2c_mux_core)
            + (size_t)muxc->max_adapters * sizeof(struct i2c_adapter *));
    struct i2c_adapter *adap = &child_storage[muxc->num_adapters];

    memset(adap, 0, sizeof(*adap));
    adap->algo      = &mux_algo;
    adap->algo_data = muxc;
    snprintf(adap->name, sizeof(adap->name), "i2c-mux-ch%u", chan_id);

    muxc->adapter[muxc->num_adapters] = adap;
    muxc->num_adapters++;
    return 0;
}

void i2c_mux_del_adapters(struct i2c_mux_core *muxc) {
    if (!muxc) return;
    /* Children live inside the muxc allocation; freeing the core
     * disposes of them in one shot. Match upstream's
     * "del all + free" semantic by zeroing num_adapters first so any
     * concurrent lookup observes a torn-down core. */
    muxc->num_adapters = 0;
    free(muxc);
}
