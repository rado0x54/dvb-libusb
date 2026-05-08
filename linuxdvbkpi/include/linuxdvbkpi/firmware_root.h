/* SPDX-License-Identifier: MIT */
/*
 * Tiny API for upper-layer code to point the polyfill's
 * request_firmware() at a specific directory at process startup.
 * Pulled out into its own header so consumers can use it without
 * dragging in the entire kernel-API polyfill surface (which has
 * uAPI enum definitions that may collide with the consumer's own
 * dvb.h when both are in scope at once — typical for SAT>IP /
 * DVB-server upper layers built around the kernel's headers).
 */

#ifndef LINUXDVBKPI_FIRMWARE_ROOT_H
#define LINUXDVBKPI_FIRMWARE_ROOT_H

#ifdef __cplusplus
extern "C" {
#endif

void linuxdvbkpi_set_firmware_root(const char *path);

#ifdef __cplusplus
}
#endif

#endif
