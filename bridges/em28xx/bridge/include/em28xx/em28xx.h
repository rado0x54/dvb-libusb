/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userspace port of drivers/media/usb/em28xx/ (Empia em28xx USB →
 * I²C/TS bridge family). This file is the public surface; everything
 * else lives in src/.
 *
 * Origin: torvalds/linux @ 9207d47f966be9f4d52e7e0119ac2b7a7e366f3e
 * (mirrored locally for porting, not committed to this repo).
 *
 * License: GPL-2.0-or-later. The upstream driver is GPL-2.0+; the
 * userspace port preserves that.
 *
 * Authors of the upstream driver this code is derived from:
 *   Ludovico Cavedon <cavedon@sssup.it>
 *   Markus Rechberger <mrechberger@gmail.com>
 *   Mauro Carvalho Chehab <mchehab@kernel.org>
 *   Sascha Sommer <saschasommer@freenet.de>
 *   Frank Schäfer <fschaefer.oss@googlemail.com>
 *   …and others. See drivers/media/usb/em28xx/ in the kernel tree
 *   for the full author list.
 *
 * Userspace-port scope:
 *   - Phase C2 (this file's first cut): em28xx_read_reg /
 *     em28xx_write_regs / em28xx_write_reg / em28xx_write_reg_bits
 *     over usbq_control. Enough to read the chip-ID register
 *     and verify we're talking to em28174.
 *   - Phase C3: I²C-over-USB master (em28xx_i2c.c port).
 *   - Phase C4+: streaming + DVB binding.
 *
 * The struct em28xx_dev opaque handle wraps a usbq device + the
 * little bit of per-device state the bridge needs (chip ID,
 * disconnected flag, an mmap'd URB scratch buffer matching
 * upstream's `dev->urb_buf`). One handle per physical USB device.
 */

#ifndef EM28XX_EM28XX_H
#define EM28XX_EM28XX_H

#include <stddef.h>
#include <stdint.h>

#include "usbq/usbq.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Subset of em28xx-reg.h needed at the public surface. */
#define EM28XX_R06_I2C_CLK         0x06
#define EM28XX_R0A_CHIPID          0x0a
#define EM28XX_R0F_XCLK            0x0f
#define EM28XX_R12_VINENABLE       0x12
#define EM2874_R5D_TS1_PKT_SIZE    0x5d
#define EM2874_R5E_TS2_PKT_SIZE    0x5e
#define EM2874_R5F_TS_ENABLE       0x5f
#define EM2874_R80_GPIO_P0_CTRL    0x80

/* I²C clock register (0x06) bits / fields. */
#define EM28XX_I2C_CLK_ACK_LAST_READ    0x80
#define EM28XX_I2C_CLK_WAIT_ENABLE      0x40
#define EM2874_I2C_SECONDARY_BUS_SELECT 0x04
#define EM28XX_I2C_FREQ_1_5_MHZ         0x03
#define EM28XX_I2C_FREQ_25_KHZ          0x02
#define EM28XX_I2C_FREQ_400_KHZ         0x01
#define EM28XX_I2C_FREQ_100_KHZ         0x00

/* XCLK register (0x0f) bits — needed for em28xx_set_xclk(). */
#define EM28XX_XCLK_AUDIO_UNMUTE        0x80
#define EM28XX_XCLK_I2S_MSB_TIMING      0x40
#define EM28XX_XCLK_IR_RC5_MODE         0x20
#define EM28XX_XCLK_IR_NEC_CHK_PARITY   0x10
#define EM28XX_XCLK_FREQUENCY_30MHZ     0x00
#define EM28XX_XCLK_FREQUENCY_15MHZ     0x01
#define EM28XX_XCLK_FREQUENCY_10MHZ     0x02
#define EM28XX_XCLK_FREQUENCY_7_5MHZ    0x03
#define EM28XX_XCLK_FREQUENCY_6MHZ      0x04
#define EM28XX_XCLK_FREQUENCY_12MHZ     0x07

/* Subset of enum em28xx_chip_id from em28xx-reg.h. */
enum em28xx_chip_id {
    EM28XX_CHIP_ID_EM2874  = 65,
    EM28XX_CHIP_ID_EM2884  = 68,
    EM28XX_CHIP_ID_EM28174 = 113, /* WinTV-dualHD bridge */
    EM28XX_CHIP_ID_EM28178 = 114,
};

typedef struct em28xx_dev em28xx_dev_t;

/* Open the em28xx bridge over an already-open usbq device. The
 * usbq device must have its USB interface 0 claimed by the caller
 * (the bridge does not claim/release interfaces — that's policy).
 *
 * Returns NULL on allocation failure or if the underlying device is
 * NULL. The returned handle borrows `usb`; the caller still owns
 * the usbq device and must outlive the em28xx handle. */
em28xx_dev_t *em28xx_open(usbq_dev_t *usb);

/* Close the bridge handle. Does NOT close the usbq device. */
void em28xx_close(em28xx_dev_t *dev);

/* Direct register R/W. The em28xx ABI is a vendor control transfer:
 *   - read:  bmRequestType=0xC0 (IN | VENDOR | DEVICE), bRequest=req,
 *            wValue=0x0000, wIndex=reg, wLength=len
 *   - write: bmRequestType=0x40 (OUT | VENDOR | DEVICE), same shape.
 *
 * The bRequest byte is overloaded:
 *   req = 0  (USB_REQ_GET_STATUS) — bridge-register access
 *   req = 2  — i2c "stop after this" transfer
 *   req = 3  — i2c "no stop" (continued) write
 * The reg-access wrappers (em28xx_read_reg / em28xx_write_regs) call
 * the _req variants with req=0; the i2c layer uses req=2/3 to drive
 * downstream chips through the bridge.
 *
 * em28xx_read_reg* returns the register byte (0..255) on success or
 * a negative errno-style error code (-EIO etc.). The _len form returns
 * the number of bytes read on success. em28xx_write_regs returns 0
 * on success / negative on error. */
int em28xx_read_reg_req_len (em28xx_dev_t *dev, uint8_t req, uint16_t reg,
                             void *buf, int len);
int em28xx_write_regs_req   (em28xx_dev_t *dev, uint8_t req, uint16_t reg,
                             const void *buf, int len);
int em28xx_read_reg     (em28xx_dev_t *dev, uint16_t reg);
int em28xx_read_reg_len (em28xx_dev_t *dev, uint16_t reg,
                         void *buf, int len);
int em28xx_write_regs   (em28xx_dev_t *dev, uint16_t reg,
                         const void *buf, int len);
int em28xx_write_reg    (em28xx_dev_t *dev, uint16_t reg, uint8_t val);
int em28xx_write_reg_bits(em28xx_dev_t *dev, uint16_t reg,
                          uint8_t val, uint8_t bitmask);

/* ---- I²C-over-USB master ----
 *
 * Direct port of em28xx-i2c.c's EM28XX_I2C_ALGO_EM28XX algorithm,
 * which is what every em28174 / em28178 board (including the WinTV-
 * dualHD) uses. The em2800 and em25xx_bus_b algos are out of scope.
 *
 * struct em28xx_i2c_msg matches the kernel's struct i2c_msg shape:
 * 7-bit slave addr, optional read flag, byte buffer + length. We use
 * our own struct rather than including <linux/i2c.h> to keep the
 * userspace surface self-contained.
 *
 * em28xx_i2c_xfer drives the array msgs[0..num) over bus `bus`
 * (0 or 1) and returns `num` on success, or a negative errno on
 * failure (-ENXIO = no device at addr, -ETIMEDOUT = clock stretch,
 * -EOPNOTSUPP = msg too long, etc.).
 *
 * The hardware caps per-message len at 64 bytes (USB control-msg
 * length limit). Higher-level chip drivers (si2168, si2157) split
 * larger payloads themselves.
 *
 * Calling em28xx_i2c_set_speed before the first xfer is recommended
 * (board-specific value, e.g. EM28XX_I2C_FREQ_400_KHZ |
 * EM28XX_I2C_CLK_WAIT_ENABLE for the WinTV-dualHD). It can be
 * called once at init or every time you switch buses. */

#define EM28XX_I2C_M_RD 0x0001 /* read flag — same value as Linux's I2C_M_RD */

#define EM28XX_I2C_MAX_MSG_LEN 64 /* USB control-msg limit */

typedef struct em28xx_i2c_msg {
    uint16_t addr;        /* 7-bit slave addr (un-shifted; we shift internally) */
    uint16_t flags;       /* EM28XX_I2C_M_RD | 0 */
    uint16_t len;         /* 0 = check-for-device probe */
    uint8_t *buf;
} em28xx_i2c_msg_t;

int em28xx_i2c_set_speed(em28xx_dev_t *dev, uint8_t i2c_clk_reg_val);

int em28xx_i2c_xfer(em28xx_dev_t *dev, int bus,
                    em28xx_i2c_msg_t *msgs, int num);

/* linuxdvbkpi `struct i2c_adapter` shim for `bus` (0 or 1). Hands
 * the resulting adapter to upstream chip drivers (lifted si2168 /
 * si2157) via i2c_new_client_device(). The adapter's lifetime is
 * the bridge handle's; do not free or unregister manually. Returns
 * NULL on invalid bus. */
struct i2c_adapter;
struct i2c_adapter *em28xx_get_i2c_adapter(em28xx_dev_t *dev, int bus);

/* ---- Bridge mode + GPIO sequences ----
 *
 * Direct port of upstream em28xx_set_mode + em28xx_gpio_set, used
 * to apply the per-board "tuner_gpio" / "dvb_gpio" reg-sequences
 * that condition the bridge for digital-DVB or analog-capture
 * operation. Without running the right sequence:
 *
 *   - the demod-reset GPIOs are never pulsed (chip starts from POR
 *     and may misbehave),
 *   - LED-control GPIO bits stay in their default state (LEDs off),
 *   - TS-bus packet-size + enable registers default to disabled.
 *
 * The em28174 (WinTV-dualHD bridge) needs this. */

enum em28xx_mode {
    EM28XX_MODE_SUSPEND = 0,  /* skips the prelude (0x48 + VINENABLE) */
    EM28XX_MODE_ANALOG  = 1,
    EM28XX_MODE_DIGITAL = 2,
};

/* Mirrors upstream's struct em28xx_reg_seq. End-of-sequence is
 * marked by sleep < 0 (any negative); a "sleep only, no register
 * write" step uses reg < 0. mask=0xff means write all 8 bits. */
typedef struct em28xx_reg_seq {
    int reg;
    int val;
    int mask;
    int sleep_ms;
} em28xx_reg_seq_t;

/* Convenience: writes EM28XX_R0F_XCLK with the given value. The
 * em28xx default for boards that don't specify is
 * (EM28XX_XCLK_IR_RC5_MODE | EM28XX_XCLK_FREQUENCY_12MHZ). */
int em28xx_set_xclk(em28xx_dev_t *dev, uint8_t xclk);

/* Apply a per-board GPIO reg-sequence. If `mode` != SUSPEND, runs
 * the prelude (writes register 0x48 = 0x00, EM28XX_R12_VINENABLE
 * = 0x37 for digital / 0x67 for analog, 10 ms settle) before the
 * sequence. Each step in `seq`:
 *
 *   - writes reg `step.val` (masked by `step.mask`) if step.reg >= 0,
 *   - sleeps `step.sleep_ms` ms after the write (0 = no sleep),
 *   - terminates when sleep_ms < 0.
 *
 * Returns 0 on success, negative on the first reg-write that
 * fails. Pass NULL for `seq` to run only the prelude. */
int em28xx_gpio_set(em28xx_dev_t *dev, enum em28xx_mode mode,
                    const em28xx_reg_seq_t *seq);

/* ---- TS streaming control ---- */

/* TS-bus enable register (0x5f) bit fields, per em28xx-reg.h. */
#define EM2874_TS1_CAPTURE_ENABLE 0x01
#define EM2874_TS1_FILTER_ENABLE  0x02
#define EM2874_TS1_NULL_DISCARD   0x04
#define EM2874_TS2_CAPTURE_ENABLE 0x10
#define EM2874_TS2_FILTER_ENABLE  0x20
#define EM2874_TS2_NULL_DISCARD   0x40

/* USB bulk-IN endpoints used for TS data on em28174 / em28178 boards
 * with `has_dual_ts` (e.g. WinTV-dualHD). Per upstream's endpoint
 * scan in em28xx-cards.c (lines 4038–4072). */
#define EM28XX_TS1_BULK_EP 0x84  /* primary frontend */
#define EM28XX_TS2_BULK_EP 0x85  /* secondary frontend */

/* Enable / disable TS capture for the given TS index (0 = TS1
 * primary, 1 = TS2 secondary). On enable, also sets the bulk-mode
 * packet-size register (0x5d/0x5e = 0xff = 48128-byte max group)
 * matching upstream's bulk-mode em28xx_capture_start.
 *
 * Caller is responsible for already having run the bridge bring-up
 * (em28xx_gpio_set) and tuned the demod (set_frontend) before
 * calling this. */
int em28xx_capture_start(em28xx_dev_t *dev, int ts_index, int enable);

/* One-time "wire up the second TS endpoint" sequence for boards with
 * has_dual_ts (e.g. WinTV-dualHD). Direct port of the bulk-mode block
 * in upstream em28xx_usb_probe (em28xx-cards.c, comment "Configure
 * hardware to support TS2"): write R0B = 0x96, sleep 100 ms, write
 * R0B = 0x80, sleep 100 ms. Skipping this leaves TS1 functional but
 * TS2 disconnected — EP 0x85 emits a frozen 4-byte filler pattern
 * even though the second demod locks normally.
 *
 * Call once at device bring-up, after em28xx_gpio_set and before
 * frontend init / capture_start. Bulk transfer mode only; the ISOC
 * variant (final byte 0x82) is not exposed since usbq streams
 * everything via bulk. */
int em28xx_enable_dual_ts_bulk(em28xx_dev_t *dev);

/* Cached chip ID — populated by em28xx_open() via a single read of
 * EM28XX_R0A_CHIPID. Returns 0 if open() failed to read it.
 * Compare against EM28XX_CHIP_ID_*. */
int em28xx_chip_id(const em28xx_dev_t *dev);

/* Returns the underlying usbq device handle. Borrowed pointer —
 * the em28xx layer doesn't own the USB device, the caller does.
 * Used by upper layers (engine_wintv) that need to issue bulk
 * reads on the TS endpoint without going through em28xx's
 * register-R/W path. */
usbq_dev_t *em28xx_usb_handle(em28xx_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EM28XX_EM28XX_H */
