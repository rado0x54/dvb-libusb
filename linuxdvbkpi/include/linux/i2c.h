/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * i2c subsystem polyfill — ABI-compatible with the kernel's
 * <linux/i2c.h> for the subset chip drivers consume:
 *
 *   - struct i2c_msg (addr, flags, len, buf)
 *   - struct i2c_adapter (we expose master_xfer + algo_data + dev)
 *   - struct i2c_client (adapter + addr + dev pointer)
 *   - struct i2c_board_info (name string + addr + platform_data)
 *   - struct i2c_driver (probe + id_table + .driver.name)
 *   - i2c_transfer / i2c_master_send / i2c_master_recv
 *   - i2c_get/set_clientdata, i2c_get/set_adapdata
 *   - i2c_new_client_device / i2c_new_dummy_device / i2c_unregister_device
 *   - module_i2c_driver(drv) — process-local registry constructor
 *
 * Driver-registration model:
 *
 *   `module_i2c_driver(foo_driver)` expands to a __attribute__((constructor))
 *   that registers `foo_driver` in a static linked list at process
 *   start. `i2c_new_client_device(adap, info)` looks up by `info->type`
 *   in that list, allocates an i2c_client, copies `info->platform_data`
 *   into `client->dev.platform_data`, and calls `driver->probe(client)`.
 *   Same surface as the kernel; same chip-driver code compiles unchanged.
 */

#ifndef LINUXDVBKPI_LINUX_I2C_H
#define LINUXDVBKPI_LINUX_I2C_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mutex.h>

#define I2C_NAME_SIZE  20

/* i2c_msg flags. Same values as the kernel's. */
#define I2C_M_RD             0x0001
#define I2C_M_TEN            0x0010
#define I2C_M_DMA_SAFE       0x0200
#define I2C_M_RECV_LEN       0x0400
#define I2C_M_NO_RD_ACK      0x0800
#define I2C_M_IGNORE_NAK     0x1000
#define I2C_M_REV_DIR_ADDR   0x2000
#define I2C_M_NOSTART        0x4000
#define I2C_M_STOP           0x8000

/* Adapter functionality bits — chip drivers occasionally check, but
 * we report the I2C_FUNC_I2C only (so master-mode multi-byte xfers
 * are fine; SMBus quirks aren't). */
#define I2C_FUNC_I2C                       0x00000001
#define I2C_FUNC_SMBUS_BYTE_DATA           0x00000020
#define I2C_FUNC_SMBUS_WORD_DATA           0x00000040
#define I2C_FUNC_SMBUS_I2C_BLOCK           0x04000000

struct i2c_msg {
    u16 addr;        /* 7-bit slave address */
    u16 flags;       /* I2C_M_* */
    u16 len;
    u8 *buf;
};

struct i2c_adapter;

struct i2c_algorithm {
    int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
    u32 (*functionality)(struct i2c_adapter *adap);
};

struct i2c_adapter {
    const struct i2c_algorithm *algo;
    void                       *algo_data;     /* upstream calls this `priv` */
    void                       *driver_data;   /* set by i2c_set_adapdata */
    struct device               dev;
    char                        name[48];
};

struct i2c_device_id {
    char     name[I2C_NAME_SIZE];
    unsigned long driver_data;
};

struct i2c_client {
    struct i2c_adapter *adapter;
    u16                 addr;
    u16                 flags;
    char                name[I2C_NAME_SIZE];
    struct device       dev;
    /* The matched id_table entry from the bound driver. Populated by
     * i2c_new_client_device(); NULL if no driver bound. Returned by
     * i2c_client_get_device_id(). */
    const struct i2c_device_id *device_id;
};

struct i2c_board_info {
    char  type[I2C_NAME_SIZE];
    u16   flags;
    u16   addr;
    void *platform_data;
};

struct i2c_driver {
    struct device_driver driver;
    int  (*probe)(struct i2c_client *client);
    void (*remove)(struct i2c_client *client);
    const struct i2c_device_id *id_table;
};

/* ---- adapter helpers ---- */

static inline void *i2c_get_adapdata(const struct i2c_adapter *adap) {
    return adap ? adap->driver_data : 0;
}

static inline void i2c_set_adapdata(struct i2c_adapter *adap, void *p) {
    if (adap) adap->driver_data = p;
}

static inline void *i2c_get_clientdata(const struct i2c_client *client) {
    return client ? client->dev.driver_data : 0;
}

static inline void i2c_set_clientdata(struct i2c_client *client, void *p) {
    if (client) client->dev.driver_data = p;
}

/* ---- transfer primitives ---- */

#ifdef __cplusplus
extern "C" {
#endif

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
int i2c_master_send(const struct i2c_client *client, const char *buf, int count);
int i2c_master_recv(const struct i2c_client *client, char *buf, int count);

/* ---- driver registry + new_client_device ---- */

void linuxdvbkpi_register_i2c_driver(struct i2c_driver *drv);

struct i2c_client *i2c_new_client_device(struct i2c_adapter *adap,
                                         const struct i2c_board_info *info);
struct i2c_client *i2c_new_dummy_device(struct i2c_adapter *adap, u16 addr);
void               i2c_unregister_device(struct i2c_client *client);

/* upstream's `i2c_client_has_driver` checks dev.driver != NULL. We
 * set it on successful probe (binding), so the upstream test works. */
static inline int i2c_client_has_driver(const struct i2c_client *client) {
    return client && client->dev.driver != 0;
}

/* i2c_adapter_id — kernel returns the adapter's bus number. We don't
 * track per-adapter IDs; return a stable hash of the pointer so chip
 * drivers can use it as a debug print value. */
static inline int i2c_adapter_id(struct i2c_adapter *adap) {
    return (int)(((uintptr_t)adap >> 4) & 0xffff);
}

/* The i2c_device_id entry from the bound driver's id_table that
 * matched this client's `info->type`. NULL if no driver bound.
 * Chip drivers (si2157) read driver_data off this to know which
 * chip variant they're attached to. */
static inline const struct i2c_device_id *
i2c_client_get_device_id(const struct i2c_client *client) {
    return client ? client->device_id : 0;
}

#ifdef __cplusplus
}
#endif

/* `module_i2c_driver(drv)` — register at process start. */
#define module_i2c_driver(__driver)                                          \
    __attribute__((constructor))                                             \
    static void __linuxdvbkpi_register_##__driver(void) {                    \
        linuxdvbkpi_register_i2c_driver(&__driver);                          \
    }

/* devm_* helpers — chip drivers expect them to be tied to dev lifetime,
 * but in our shim we do explicit cleanup. We treat devm_ as plain. */
#define devm_kzalloc(dev, size, flags) kzalloc(size, flags)
#define devm_kmalloc(dev, size, flags) kmalloc(size, flags)
#define devm_kfree(dev, p) kfree(p)

#endif
