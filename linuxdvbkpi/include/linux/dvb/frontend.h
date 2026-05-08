/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Userspace mn88472.h / tda18250.h include <linux/dvb/frontend.h> for
 * the uAPI enums (fe_delivery_system etc.). Our consolidated polyfill
 * declares them in <media/dvb_frontend.h>; forward to that. */
#ifndef LINUXDVBKPI_LINUX_DVB_FRONTEND_H
#define LINUXDVBKPI_LINUX_DVB_FRONTEND_H
#include <media/dvb_frontend.h>
#endif
