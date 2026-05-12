/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_DEVICE_H
#define LINUXDVBKPI_LINUX_DEVICE_H

#include <linux/types.h>
#include <linux/printk.h>

/* Just enough of `struct device` for chip drivers' dev_*() macros and
 * platform_data lookup. The actual i2c_client is composed in
 * linux/i2c.h; this header keeps the device + dev_dbg/dev_err macros
 * separate so it can be included on its own. */

struct device_driver {
    const char *name;
    void *owner;                 /* THIS_MODULE — opaque */
    int   suppress_bind_attrs;   /* unused in userspace; accepted for compat */
};

struct device {
    void              *platform_data;
    void              *driver_data;
    struct device_driver *driver;
    char               name[64];   /* used by dev_*() prefix */
    struct device     *parent;
};

static inline void *dev_get_drvdata(const struct device *d) {
    return d ? d->driver_data : 0;
}

static inline void dev_set_drvdata(struct device *d, void *p) {
    if (d) d->driver_data = p;
}

#define dev_name(d) ((d)->name)

#ifndef LINUXDVBKPI_VERBOSE
#define LINUXDVBKPI_VERBOSE 0
#endif

#define dev_err(d, fmt, ...) \
    fprintf(stderr, "[lkpi:err  %s] " fmt, dev_name(d), ##__VA_ARGS__)
#define dev_warn(d, fmt, ...) \
    fprintf(stderr, "[lkpi:warn %s] " fmt, dev_name(d), ##__VA_ARGS__)
#define dev_info(d, fmt, ...) \
    fprintf(stderr, "[lkpi:info %s] " fmt, dev_name(d), ##__VA_ARGS__)
#define dev_notice(d, fmt, ...) \
    fprintf(stderr, "[lkpi:note %s] " fmt, dev_name(d), ##__VA_ARGS__)

#if LINUXDVBKPI_VERBOSE
#define dev_dbg(d, fmt, ...) \
    fprintf(stderr, "[lkpi:dbg  %s] " fmt, dev_name(d), ##__VA_ARGS__)
#else
#define dev_dbg(d, fmt, ...) ((void)0)
#endif

#define dev_err_once  dev_err
#define dev_warn_once dev_warn

#endif
