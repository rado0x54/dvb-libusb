/* SPDX-License-Identifier: MIT */
/*
 * request_firmware / release_firmware polyfill.
 *
 * Lookup order:
 *   1. Path explicitly set via linuxdvbkpi_set_firmware_root().
 *   2. $FIRMWARE_DIR env var (a directory).
 *   3. Common system paths (/usr/local/lib/firmware, /usr/lib/firmware,
 *      /lib/firmware).
 *
 * On success allocates a `struct firmware` plus a malloc'd byte
 * buffer; release_firmware frees both. The blobs are tiny (8 KiB
 * mn88472, 60 KiB dib0700) so malloc is fine — no need for mmap.
 */

#include <linuxdvbkpi/linuxdvbkpi.h>
#include <linux/firmware.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static pthread_mutex_t g_root_lock = PTHREAD_MUTEX_INITIALIZER;
static char            g_root[1024] = {0};

void linuxdvbkpi_set_firmware_root(const char *path) {
    pthread_mutex_lock(&g_root_lock);
    if (!path) {
        g_root[0] = '\0';
    } else {
        strncpy(g_root, path, sizeof(g_root) - 1);
        g_root[sizeof(g_root) - 1] = '\0';
    }
    pthread_mutex_unlock(&g_root_lock);
}

static int try_load(const char *dir, const char *name,
                    const struct firmware **out) {
    if (!dir || !name) return -EINVAL;
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    struct stat st;
    if (stat(path, &st) < 0) return -ENOENT;
    if (!S_ISREG(st.st_mode)) return -ENOENT;

    FILE *f = fopen(path, "rb");
    if (!f) return -ENOENT;

    size_t size = (size_t)st.st_size;
    u8 *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -ENOMEM;
    }
    size_t got = fread(buf, 1, size, f);
    fclose(f);
    if (got != size) {
        free(buf);
        return -EIO;
    }

    struct firmware *fw = calloc(1, sizeof(*fw));
    if (!fw) {
        free(buf);
        return -ENOMEM;
    }
    fw->size = size;
    fw->data = buf;
    *out = fw;
    return 0;
}

int request_firmware(const struct firmware **out, const char *name,
                     struct device *dev) {
    (void)dev;
    if (!out || !name) return -EINVAL;
    *out = 0;

    char root[1024];
    pthread_mutex_lock(&g_root_lock);
    strncpy(root, g_root, sizeof(root));
    root[sizeof(root) - 1] = '\0';
    pthread_mutex_unlock(&g_root_lock);
    if (root[0] && try_load(root, name, out) == 0) return 0;

    const char *env = getenv("FIRMWARE_DIR");
    if (env && env[0] && try_load(env, name, out) == 0) return 0;

    static const char *fallbacks[] = {
        "/usr/local/lib/firmware",
        "/usr/lib/firmware",
        "/lib/firmware",
        0,
    };
    for (size_t i = 0; fallbacks[i]; i++) {
        if (try_load(fallbacks[i], name, out) == 0) return 0;
    }
    return -ENOENT;
}

void release_firmware(const struct firmware *fw) {
    if (!fw) return;
    if (fw->data) free((void *)fw->data);
    free((void *)fw);
}
