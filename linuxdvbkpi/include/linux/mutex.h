/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LINUXDVBKPI_LINUX_MUTEX_H
#define LINUXDVBKPI_LINUX_MUTEX_H

#include <pthread.h>

struct mutex {
    pthread_mutex_t lock;
};

static inline void mutex_init(struct mutex *m) {
    pthread_mutex_init(&m->lock, NULL);
}

static inline void mutex_destroy(struct mutex *m) {
    pthread_mutex_destroy(&m->lock);
}

static inline void mutex_lock(struct mutex *m) {
    pthread_mutex_lock(&m->lock);
}

static inline void mutex_unlock(struct mutex *m) {
    pthread_mutex_unlock(&m->lock);
}

static inline int mutex_lock_interruptible(struct mutex *m) {
    pthread_mutex_lock(&m->lock);
    return 0;
}

#define DEFINE_MUTEX(name) \
    struct mutex name = { .lock = PTHREAD_MUTEX_INITIALIZER }

#endif
