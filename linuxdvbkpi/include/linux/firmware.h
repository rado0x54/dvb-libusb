/* SPDX-License-Identifier: MIT */
#ifndef LINUXDVBKPI_LINUX_FIRMWARE_H
#define LINUXDVBKPI_LINUX_FIRMWARE_H

#include <linux/types.h>
#include <linux/device.h>

struct firmware {
    size_t   size;
    const u8 *data;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves a firmware blob by name. Lookup order:
 *   1. Explicit root from linuxdvbkpi_set_firmware_root() (if set)
 *   2. $FIRMWARE_DIR env var (if set)
 *   3. Common system paths (/usr/local/lib/firmware, /usr/lib/firmware,
 *      /lib/firmware)
 *
 * Returns 0 on success and writes a *fw with .data malloc'd
 * (zero-copy mmap would also work but malloc is simpler and the
 * blobs are tiny — 8 KiB demod fw, 60 KiB bridge fw). Caller frees
 * via release_firmware(). Returns negative errno on failure
 * (-ENOENT if not found anywhere, -EIO on read error). */
int  request_firmware(const struct firmware **fw, const char *name,
                      struct device *dev);
void release_firmware(const struct firmware *fw);

/* firmware_request_nowarn — same as request_firmware but suppresses
 * the kernel-side warning when the blob isn't found. Our resolver
 * doesn't log on miss, so the two behave identically. */
static inline int firmware_request_nowarn(const struct firmware **fw,
                                          const char *name,
                                          struct device *dev) {
    return request_firmware(fw, name, dev);
}

#ifdef __cplusplus
}
#endif

#endif
