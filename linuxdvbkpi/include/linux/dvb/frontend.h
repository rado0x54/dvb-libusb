/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Userspace mn88472.h / tda18250.h include <linux/dvb/frontend.h> for
 * the uAPI enums (fe_delivery_system etc.). Our consolidated polyfill
 * declares them in <media/dvb_frontend.h>; forward to that.
 *
 * Additionally, the real Linux kernel UAPI <linux/dvb/frontend.h>
 * historically exposes `typedef enum X X_t` aliases for each of these
 * enums — userspace apps (e.g. minisatip) reference the `_t` form.
 * media/dvb_frontend.h declares only the bare enums, so when this
 * polyfill is found before the system UAPI header on Linux those
 * typedefs go missing. Define them here once the enums are visible. */
#ifndef LINUXDVBKPI_LINUX_DVB_FRONTEND_H
#define LINUXDVBKPI_LINUX_DVB_FRONTEND_H
#include <media/dvb_frontend.h>

typedef enum fe_status            fe_status_t;
typedef enum fe_delivery_system   fe_delivery_system_t;
typedef enum fe_modulation        fe_modulation_t;
typedef enum fe_inversion         fe_spectral_inversion_t;
typedef enum fe_code_rate         fe_code_rate_t;
typedef enum fe_transmit_mode     fe_transmit_mode_t;
typedef enum fe_guard_interval    fe_guard_interval_t;
typedef enum fe_hierarchy         fe_hierarchy_t;
typedef enum fe_pilot             fe_pilot_t;
typedef enum fe_rolloff           fe_rolloff_t;

#endif
