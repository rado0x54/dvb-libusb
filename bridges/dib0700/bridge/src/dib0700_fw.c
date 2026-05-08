/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dib0700 firmware upload.
 *
 * The dvb-usb-dib0700-1.20.fw blob is a stream of dib-flavoured
 * Intel HEX records:
 *
 *   byte 0:        len
 *   bytes 1..2:    16-bit address (high byte first, but only used as
 *                  a 16-bit value here; type 0x04 records steal 32-bit
 *                  via the next two bytes for the linear-address upper
 *                  half)
 *   byte 3:        record type
 *   bytes 4..4+len-1: payload
 *   byte 4+len:    checksum
 *
 * Each record is repackaged into a single bulk-OUT URB to the bridge's
 * fw-load endpoint (0x01) with the layout:
 *
 *   buf[0]       = len
 *   buf[1..2]    = addr (high, low)
 *   buf[3]       = type
 *   buf[4..4+len]= payload + checksum
 *
 * After the last record, REQUEST_JUMPRAM at 0x70000000 hands control
 * to the just-uploaded ramcode. Wait ~500 ms for the chip to come
 * online before issuing further vendor requests.
 */

#include "dib0700_priv.h"

#include "usbq/usbq.h"

#include <linux/firmware.h>
#include <linuxdvbkpi/linuxdvbkpi.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int dib0700_jumpram(struct dib0700_dev *dev, uint32_t address) {
    uint8_t buf[8] = {
        DIB0700_REQ_JUMPRAM, 0, 0, 0,
        (uint8_t)(address >> 24), (uint8_t)(address >> 16),
        (uint8_t)(address >>  8), (uint8_t)(address      ),
    };
    int rc = usbq_bulk_write(dev->usb, DIB0700_FW_BULK_EP,
                             buf, sizeof(buf),
                             DIB0700_CTRL_TIMEOUT_MS);
    return rc == (int)sizeof(buf) ? 0 : (rc < 0 ? rc : -EIO);
}

/* Read the entire firmware blob into a malloc'd buffer. Caller frees. */
static int slurp_file(const char *path, uint8_t **out_buf, size_t *out_size) {
    if (!path || !out_buf || !out_size) return -EINVAL;
    FILE *f = fopen(path, "rb");
    if (!f) return -ENOENT;
    struct stat st;
    if (fstat(fileno(f), &st) < 0) { fclose(f); return -EIO; }
    size_t size = (size_t)st.st_size;
    uint8_t *buf = malloc(size);
    if (!buf) { fclose(f); return -ENOMEM; }
    if (fread(buf, 1, size, f) != size) {
        free(buf); fclose(f); return -EIO;
    }
    fclose(f);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

int dib0700_download_firmware(dib0700_dev_t *dev, const char *fw_path) {
    if (!dev || !fw_path) return -EINVAL;

    uint8_t *fw_buf = NULL;
    size_t   fw_size = 0;
    int rc = slurp_file(fw_path, &fw_buf, &fw_size);
    if (rc < 0) return rc;

    /* Walk hex records. */
    uint8_t pkt[260];
    size_t pos = 0;
    while (pos < fw_size) {
        if (pos + 4 > fw_size) { rc = -EINVAL; goto done; }
        uint8_t  rec_len   = fw_buf[pos + 0];
        uint16_t rec_addr  = (uint16_t)(fw_buf[pos + 1] | (fw_buf[pos + 2] << 8));
        uint8_t  rec_type  = fw_buf[pos + 3];
        size_t   data_offs = 4;
        if (rec_type == 0x04) {
            /* Extended-linear-address record — payload contains the
             * upper 16 bits of a 32-bit address. We don't actually
             * use the high half; the bridge is byte-addressed. */
        }
        if (pos + rec_len + data_offs + 1 > fw_size) {
            rc = -EINVAL; goto done;
        }
        uint8_t rec_chk = fw_buf[pos + rec_len + data_offs];

        pkt[0] = rec_len;
        pkt[1] = (uint8_t)(rec_addr >> 8);
        pkt[2] = (uint8_t)(rec_addr & 0xff);
        pkt[3] = rec_type;
        memcpy(&pkt[4], &fw_buf[pos + data_offs], rec_len);
        pkt[4 + rec_len] = rec_chk;

        /* Firmware records ship via bulk-OUT on EP 0x01 — same as
         * upstream's `usb_bulk_msg(udev, sndbulkpipe(0x01), …)`.
         * Sending these as control transfers makes the bridge stall
         * the endpoint (libusb -EPIPE / -9). */
        int wr = usbq_bulk_write(dev->usb, DIB0700_FW_BULK_EP,
                                 pkt, (size_t)(rec_len + 5),
                                 DIB0700_CTRL_TIMEOUT_MS);
        if (wr < 0) {
            rc = wr;
            goto done;
        }
        if (wr != rec_len + 5) {
            rc = -EIO;
            goto done;
        }
        pos += rec_len + data_offs + 1;
    }

    rc = dib0700_jumpram(dev, 0x70000000);
    if (rc == 0) {
        /* Wait for the ramcode to come up. Upstream sleeps 500 ms. */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 500 * 1000000L };
        nanosleep(&ts, NULL);
    }

done:
    free(fw_buf);
    return rc;
}
