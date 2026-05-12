/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * workqueue polyfill — no-op stubs for delayed_work. The only
 * consumer in our lifted chip drivers (si2157) uses delayed_work to
 * schedule periodic stat polls; engines query status synchronously
 * via fe->ops.read_status, so the periodic worker is dead weight.
 *
 * Keeping the API surface here means upstream's INIT_DELAYED_WORK /
 * schedule_delayed_work / cancel_delayed_work_sync compile clean
 * without source edits. The chip's primary functionality
 * (init/sleep/set_params/read_status) is unaffected.
 */

#ifndef LINUXDVBKPI_LINUX_WORKQUEUE_H
#define LINUXDVBKPI_LINUX_WORKQUEUE_H

struct work_struct {
    int _unused;
};

struct delayed_work {
    struct work_struct work;
};

typedef void (*work_func_t)(struct work_struct *work);

#define INIT_WORK(w, fn) ((void)(w), (void)(fn))
#define INIT_DELAYED_WORK(dw, fn) ((void)(dw), (void)(fn))

#define schedule_work(w) ((void)(w), 0)
#define schedule_delayed_work(dw, delay_jiffies) \
    ((void)(dw), (void)(delay_jiffies), 0)

#define cancel_work_sync(w) ((void)(w), 0)
#define cancel_delayed_work_sync(dw) ((void)(dw), 0)

#endif /* LINUXDVBKPI_LINUX_WORKQUEUE_H */
