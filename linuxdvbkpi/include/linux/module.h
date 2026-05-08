/* SPDX-License-Identifier: MIT */
#ifndef LINUXDVBKPI_LINUX_MODULE_H
#define LINUXDVBKPI_LINUX_MODULE_H

/* Token-paste helpers so per-call identifiers don't collide when a
 * driver invokes one of these macros multiple times in the same TU
 * (si2157 declares 6 MODULE_FIRMWARE entries). */
#define LINUXDVBKPI_CAT_(a, b) a ## b
#define LINUXDVBKPI_CAT(a, b)  LINUXDVBKPI_CAT_(a, b)
#define LINUXDVBKPI_UNIQ(prefix) LINUXDVBKPI_CAT(prefix, __COUNTER__)

/* Module metadata macros — no-op in userspace. Each call gets a
 * unique identifier so multiple invocations don't redefine. */
#define MODULE_LICENSE(s)        static const char *const LINUXDVBKPI_UNIQ(__module_license_) = (s)
#define MODULE_AUTHOR(s)         static const char *const LINUXDVBKPI_UNIQ(__module_author_) = (s)
#define MODULE_DESCRIPTION(s)    static const char *const LINUXDVBKPI_UNIQ(__module_description_) = (s)
#define MODULE_VERSION(s)        static const char *const LINUXDVBKPI_UNIQ(__module_version_) = (s)
#define MODULE_ALIAS(s)          static const char *const LINUXDVBKPI_UNIQ(__module_alias_) = (s)
#define MODULE_DEVICE_TABLE(t, n) /* no-op */
#define MODULE_FIRMWARE(name)    static const char *const LINUXDVBKPI_UNIQ(__module_firmware_) = (name)

#define EXPORT_SYMBOL(s)         /* no-op */
#define EXPORT_SYMBOL_GPL(s)     /* no-op */
#define EXPORT_SYMBOL_NS(s, ns)  /* no-op */
#define EXPORT_SYMBOL_NS_GPL(s, ns) /* no-op */

/* THIS_MODULE / module_param — chip drivers don't set knobs at runtime
 * in our usage, so we discard the param-name and storage. */
#define THIS_MODULE             ((void *)0)

#define module_param(name, type, perm)              static int __unused_ ## name
#define module_param_named(alias, name, type, perm) static int __unused_ ## alias
#define MODULE_PARM_DESC(name, desc)                /* no-op */

/* `module_i2c_driver(drv)` and friends register a driver at module
 * load. In userspace we provide a registry-based hook (see linux/i2c.h)
 * that the macro hooks into via a constructor function. */
#define module_init(fn)         /* no-op — i2c_driver constructors use module_i2c_driver */
#define module_exit(fn)         /* no-op */

#define request_module(name)    (0)
#define try_module_get(m)       (1)
#define module_put(m)           ((void)0)

#endif
