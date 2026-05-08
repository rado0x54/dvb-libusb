/* SPDX-License-Identifier: MIT */
/*
 * i2c subsystem polyfill — driver registry + new_client_device flow.
 *
 * Drivers register themselves at process start via constructors emitted
 * by `module_i2c_driver(...)`. New clients are created by upper-layer
 * code calling `i2c_new_client_device(adap, &info)`, which:
 *
 *   1. Looks up the driver with id_table.name == info->type.
 *   2. Allocates an i2c_client, fills addr / adapter / dev.
 *   3. Wires `client->dev.platform_data = info->platform_data`.
 *   4. Calls `driver->probe(client)`.
 *   5. On success, sets `client->dev.driver = &driver->driver` so
 *      `i2c_client_has_driver(client)` reports true.
 *
 * `i2c_new_dummy_device` is the same flow without a probe — it just
 * allocates a bare client at the given address. Used by mn88472 to
 * obtain extra clients on the same bus for its register-bank addresses
 * (0x1a, 0x1c).
 *
 * Transfer dispatch: `i2c_transfer` calls the adapter's
 * `algo->master_xfer` directly — same as the kernel.
 */

#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <pthread.h>
#include <string.h>
#include <stdlib.h>

/* Linked-list of registered drivers. The driver structs themselves
 * live in static storage (chip-driver source declares a `static struct
 * i2c_driver` and registers it via the constructor); we just thread
 * them via this `next` pointer. To avoid touching upstream's struct
 * shape we keep the chain in a side table. */

struct driver_link {
    struct i2c_driver  *drv;
    struct driver_link *next;
};

static pthread_mutex_t   g_drv_lock = PTHREAD_MUTEX_INITIALIZER;
static struct driver_link *g_drivers = 0;

void linuxdvbkpi_register_i2c_driver(struct i2c_driver *drv) {
    if (!drv) return;
    struct driver_link *link = malloc(sizeof(*link));
    if (!link) return;
    link->drv = drv;
    pthread_mutex_lock(&g_drv_lock);
    link->next = g_drivers;
    g_drivers = link;
    pthread_mutex_unlock(&g_drv_lock);
}

/* Look up a driver matching `name` in the registry. If a match is
 * found in the driver's id_table, also return a pointer to the
 * matched i2c_device_id entry via *matched_id (chip drivers read
 * driver_data off this; see i2c_client_get_device_id). */
static struct i2c_driver *find_driver_by_name(const char *name,
                                              const struct i2c_device_id **matched_id) {
    if (matched_id) *matched_id = 0;
    pthread_mutex_lock(&g_drv_lock);
    for (struct driver_link *l = g_drivers; l; l = l->next) {
        const struct i2c_device_id *ids = l->drv->id_table;
        if (ids) {
            for (size_t i = 0; ids[i].name[0]; i++) {
                if (strncmp(ids[i].name, name, I2C_NAME_SIZE) == 0) {
                    if (matched_id) *matched_id = &ids[i];
                    pthread_mutex_unlock(&g_drv_lock);
                    return l->drv;
                }
            }
        }
        if (l->drv->driver.name &&
            strncmp(l->drv->driver.name, name, I2C_NAME_SIZE) == 0) {
            pthread_mutex_unlock(&g_drv_lock);
            return l->drv;
        }
    }
    pthread_mutex_unlock(&g_drv_lock);
    return 0;
}

/* ---- Transfer primitives ---- */

int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num) {
    if (!adap || !adap->algo || !adap->algo->master_xfer) return -EIO;
    return adap->algo->master_xfer(adap, msgs, num);
}

int i2c_master_send(const struct i2c_client *client, const char *buf, int count) {
    if (!client || !client->adapter) return -EIO;
    struct i2c_msg msg = {
        .addr  = client->addr,
        .flags = client->flags & ~I2C_M_RD,
        .len   = (u16)count,
        .buf   = (u8 *)buf,
    };
    int ret = i2c_transfer(client->adapter, &msg, 1);
    return ret == 1 ? count : (ret < 0 ? ret : -EIO);
}

int i2c_master_recv(const struct i2c_client *client, char *buf, int count) {
    if (!client || !client->adapter) return -EIO;
    struct i2c_msg msg = {
        .addr  = client->addr,
        .flags = (u16)((client->flags & ~I2C_M_RD) | I2C_M_RD),
        .len   = (u16)count,
        .buf   = (u8 *)buf,
    };
    int ret = i2c_transfer(client->adapter, &msg, 1);
    return ret == 1 ? count : (ret < 0 ? ret : -EIO);
}

/* ---- Client lifecycle ---- */

static struct i2c_client *alloc_client(struct i2c_adapter *adap, u16 addr,
                                       const char *name) {
    struct i2c_client *c = calloc(1, sizeof(*c));
    if (!c) return 0;
    c->adapter = adap;
    c->addr    = addr;
    if (name) {
        strscpy(c->name, name, sizeof(c->name));
        strscpy(c->dev.name, name, sizeof(c->dev.name));
    } else {
        snprintf(c->dev.name, sizeof(c->dev.name), "i2c-%04x-%02x",
                 (unsigned)(uintptr_t)adap & 0xffff, addr);
    }
    return c;
}

struct i2c_client *i2c_new_client_device(struct i2c_adapter *adap,
                                         const struct i2c_board_info *info) {
    if (!adap || !info) return ERR_PTR(-EINVAL);

    struct i2c_client *c = alloc_client(adap, info->addr, info->type);
    if (!c) return ERR_PTR(-ENOMEM);
    c->flags = info->flags;
    c->dev.platform_data = info->platform_data;

    const struct i2c_device_id *matched = 0;
    struct i2c_driver *drv = find_driver_by_name(info->type, &matched);
    if (!drv || !drv->probe) {
        /* Kernel returns a "ghost" client (no driver bound) that
         * later get released. We do the same — caller can check
         * i2c_client_has_driver() to detect non-binding. */
        return c;
    }

    /* Stash the matched id_table entry BEFORE probe — the chip's
     * probe will read it via i2c_client_get_device_id (see si2157). */
    c->device_id = matched;

    int ret = drv->probe(c);
    if (ret < 0) {
        free(c);
        return ERR_PTR(ret);
    }
    c->dev.driver = &drv->driver;
    return c;
}

struct i2c_client *i2c_new_dummy_device(struct i2c_adapter *adap, u16 addr) {
    if (!adap) return ERR_PTR(-EINVAL);
    char buf[I2C_NAME_SIZE];
    snprintf(buf, sizeof(buf), "dummy-%02x", addr);
    struct i2c_client *c = alloc_client(adap, addr, buf);
    if (!c) return ERR_PTR(-ENOMEM);
    return c;
}

void i2c_unregister_device(struct i2c_client *client) {
    if (!client || IS_ERR(client)) return;
    /* Find the driver that probed this client and call its remove. */
    if (client->dev.driver) {
        pthread_mutex_lock(&g_drv_lock);
        struct i2c_driver *drv = 0;
        for (struct driver_link *l = g_drivers; l; l = l->next) {
            if (&l->drv->driver == client->dev.driver) { drv = l->drv; break; }
        }
        pthread_mutex_unlock(&g_drv_lock);
        if (drv && drv->remove) drv->remove(client);
    }
    free(client);
}
