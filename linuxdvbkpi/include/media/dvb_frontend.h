/* SPDX-License-Identifier: MIT */
/*
 * Userland-side mirror of <media/dvb_frontend.h>, narrowed to the
 * subset chip drivers (demod + tuner) actually consume.
 *
 * What the kernel exports through this header that we DON'T mirror:
 *
 *   - dvb_register_frontend / dvb_unregister_frontend — they
 *     register the frontend with the dvb-core /dev/dvb namespace.
 *     We don't expose /dev/dvb; consumers of this library bring
 *     their own upper layer.
 *   - The "tuneparams" union (struct dvb_qpsk_parameters etc.) —
 *     legacy SetFrontend uAPI, superseded by dtv_frontend_properties.
 *   - dvb_frontend_event / dvb_diseqc_master_cmd — DiSEqC and event
 *     queueing belong to dvb-core; chip drivers don't poke them.
 *
 * What we DO provide:
 *
 *   struct dvb_frontend / dvb_frontend_ops / dvb_tuner_ops — the
 *   v-table chip drivers fill in. Engine code constructs a frontend,
 *   chip drivers populate ops via memcpy (mn88472, tda18250 pattern),
 *   and the engine calls fe->ops.{init,set_frontend,read_status,…}
 *   directly — no dvb-core indirection.
 *
 *   struct dtv_frontend_properties — the per-tune parameter cache.
 *   Engine writes to it before set_frontend; chip driver reads from
 *   fe->dtv_property_cache to learn what to tune to and writes
 *   back to .strength/.cnr/.block_* during read_status.
 */

#ifndef LINUXDVBKPI_MEDIA_DVB_FRONTEND_H
#define LINUXDVBKPI_MEDIA_DVB_FRONTEND_H

#include <linux/types.h>
#include <linux/i2c.h>

/* ---- enum fe_status — same bits as the kernel uAPI. ---- */
enum fe_status {
    FE_NONE             = 0x00,
    FE_HAS_SIGNAL       = 0x01,
    FE_HAS_CARRIER      = 0x02,
    FE_HAS_VITERBI      = 0x04,
    FE_HAS_SYNC         = 0x08,
    FE_HAS_LOCK         = 0x10,
    FE_TIMEDOUT         = 0x20,
    FE_REINIT           = 0x40,
};

/* ---- enum fe_delivery_system. Numeric values match the upstream
 *      uAPI exactly (include/uapi/linux/dvb/frontend.h). Chip drivers
 *      switch on these names. ---- */
enum fe_delivery_system {
    SYS_UNDEFINED       = 0,
    SYS_DVBC_ANNEX_A    = 1,
    SYS_DVBC_ANNEX_B    = 2,
    SYS_DVBT            = 3,
    SYS_DSS             = 4,
    SYS_DVBS            = 5,
    SYS_DVBS2           = 6,
    SYS_DVBH            = 7,
    SYS_ISDBT           = 8,
    SYS_ISDBS           = 9,
    SYS_ISDBC           = 10,
    SYS_ATSC            = 11,
    SYS_ATSCMH          = 12,
    SYS_DTMB            = 13,
    SYS_CMMB            = 14,
    SYS_DAB             = 15,
    SYS_DVBT2           = 16,
    SYS_TURBO           = 17,
    SYS_DVBC_ANNEX_C    = 18,
    SYS_DVBC2           = 19,
};

/* ---- enum fe_modulation ---- */
enum fe_modulation {
    QPSK = 0,
    QAM_16, QAM_32, QAM_64, QAM_128, QAM_256,
    QAM_AUTO,
    VSB_8, VSB_16,
    PSK_8,
    APSK_16, APSK_32,
    DQPSK,
    QAM_4_NR,
};

enum fe_inversion {
    INVERSION_OFF = 0,
    INVERSION_ON  = 1,
    INVERSION_AUTO = 2,
};

enum fe_code_rate {
    FEC_NONE = 0, FEC_1_2, FEC_2_3, FEC_3_4, FEC_4_5, FEC_5_6, FEC_6_7,
    FEC_7_8, FEC_8_9, FEC_AUTO, FEC_3_5, FEC_9_10, FEC_2_5, FEC_1_3, FEC_1_4,
};

enum fe_transmit_mode {
    TRANSMISSION_MODE_2K = 0, TRANSMISSION_MODE_8K, TRANSMISSION_MODE_AUTO,
    TRANSMISSION_MODE_4K, TRANSMISSION_MODE_1K, TRANSMISSION_MODE_16K,
    TRANSMISSION_MODE_32K, TRANSMISSION_MODE_C1, TRANSMISSION_MODE_C3780,
};

enum fe_guard_interval {
    GUARD_INTERVAL_1_32 = 0, GUARD_INTERVAL_1_16, GUARD_INTERVAL_1_8,
    GUARD_INTERVAL_1_4, GUARD_INTERVAL_AUTO,
    GUARD_INTERVAL_1_128, GUARD_INTERVAL_19_128, GUARD_INTERVAL_19_256,
};

enum fe_hierarchy {
    HIERARCHY_NONE = 0, HIERARCHY_1, HIERARCHY_2, HIERARCHY_4,
    HIERARCHY_AUTO,
};

enum fe_pilot {
    PILOT_ON, PILOT_OFF, PILOT_AUTO,
};

enum fe_rolloff {
    ROLLOFF_35, ROLLOFF_20, ROLLOFF_25, ROLLOFF_AUTO,
};

/* dvb_tuner_ops.info / dvb_frontend_ops.info — capability bits used
 * to compose the chip driver's `static const struct dvb_*_ops`. */
#define FE_CAN_INVERSION_AUTO          0x000001
#define FE_CAN_FEC_1_2                 0x000002
#define FE_CAN_FEC_2_3                 0x000004
#define FE_CAN_FEC_3_4                 0x000008
#define FE_CAN_FEC_4_5                 0x000010
#define FE_CAN_FEC_5_6                 0x000020
#define FE_CAN_FEC_6_7                 0x000040
#define FE_CAN_FEC_7_8                 0x000080
#define FE_CAN_FEC_8_9                 0x000100
#define FE_CAN_FEC_AUTO                0x000200
#define FE_CAN_QPSK                    0x000400
#define FE_CAN_QAM_16                  0x000800
#define FE_CAN_QAM_32                  0x001000
#define FE_CAN_QAM_64                  0x002000
#define FE_CAN_QAM_128                 0x004000
#define FE_CAN_QAM_256                 0x008000
#define FE_CAN_QAM_AUTO                0x010000
#define FE_CAN_TRANSMISSION_MODE_AUTO  0x020000
#define FE_CAN_BANDWIDTH_AUTO          0x040000
#define FE_CAN_GUARD_INTERVAL_AUTO     0x080000
#define FE_CAN_HIERARCHY_AUTO          0x100000
#define FE_CAN_8VSB                    0x200000
#define FE_CAN_16VSB                   0x400000
#define FE_HAS_EXTENDED_CAPS           0x800000
#define FE_CAN_MULTISTREAM             0x4000000
#define FE_CAN_TURBO_FEC               0x8000000
#define FE_CAN_2G_MODULATION           0x10000000
#define FE_NEEDS_BENDING               0x20000000
#define FE_CAN_RECOVER                 0x40000000
#define FE_CAN_MUTE_TS                 0x80000000

/* "no PLP filter" sentinel — what the engine writes to
 * c->stream_id when DVB-T2 PLP is unspecified. */
#define NO_STREAM_ID_FILTER  ((int)0xFFFFFFFF)

/* fe->dtv_property_cache.{strength,cnr,block_*}.stat[].scale */
enum fecap_scale_params {
    FE_SCALE_NOT_AVAILABLE = 0,
    FE_SCALE_DECIBEL       = 1,
    FE_SCALE_RELATIVE      = 2,
    FE_SCALE_COUNTER       = 3,
};

/* Enough to satisfy upstream's `c->cnr.stat[0].svalue` / .uvalue /
 * .scale reads. The kernel allows up to MAX_DTV_STATS = 4 layers
 * (per-MISO etc.); chip drivers we lift only ever touch [0]. */
#define DTV_MAX_STATS 4

struct dtv_stats {
    u8 scale;            /* enum fecap_scale_params */
    union {
        u64 uvalue;
        s64 svalue;
    };
};

struct dtv_fe_stats {
    u8 len;
    struct dtv_stats stat[DTV_MAX_STATS];
};

/* dtv_frontend_properties — the parameter cache. Single field per
 * tune parameter; chip drivers read what they need, write to the
 * stat substructs. */
struct dtv_frontend_properties {
    u32                     frequency;
    enum fe_delivery_system delivery_system;
    enum fe_modulation      modulation;
    enum fe_inversion       inversion;
    u32                     symbol_rate;     /* DVB-C only */
    u32                     bandwidth_hz;    /* DVB-T/T2/C */
    enum fe_code_rate       fec_inner;
    int                     stream_id;       /* DVB-T2 PLP; -1 / NO_STREAM_ID_FILTER for "any" */
    enum fe_transmit_mode   transmission_mode;
    enum fe_guard_interval  guard_interval;
    enum fe_hierarchy       hierarchy;
    enum fe_rolloff         rolloff;
    enum fe_pilot           pilot;

    /* Stats. */
    struct dtv_fe_stats     strength;
    struct dtv_fe_stats     cnr;
    struct dtv_fe_stats     pre_bit_error;
    struct dtv_fe_stats     pre_bit_count;
    struct dtv_fe_stats     post_bit_error;
    struct dtv_fe_stats     post_bit_count;
    struct dtv_fe_stats     block_error;
    struct dtv_fe_stats     block_count;
};

struct dvb_frontend;

/* dvbfe_algo / dvbfe_search — frontend search-algorithm API. Older
 * chip drivers (lgdt3306a) advertise DVBFE_ALGO_CUSTOM and implement
 * a search() callback returning one of the SEARCH_* values. */
enum dvbfe_algo {
    DVBFE_ALGO_HW       = (1 << 0),
    DVBFE_ALGO_SW       = (1 << 1),
    DVBFE_ALGO_CUSTOM   = (1 << 2),
    DVBFE_ALGO_RECOVERY = (1u << 31),
};

enum dvbfe_search {
    DVBFE_ALGO_SEARCH_SUCCESS  = (1 << 0),
    DVBFE_ALGO_SEARCH_ASLEEP   = (1 << 1),
    DVBFE_ALGO_SEARCH_FAILED   = (1 << 2),
    DVBFE_ALGO_SEARCH_INVALID  = (1 << 3),
    DVBFE_ALGO_SEARCH_AGAIN    = (1 << 4),
    DVBFE_ALGO_SEARCH_ERROR    = (1u << 31),
};

struct dvb_frontend_tune_settings {
    int min_delay_ms;
    int step_size;
    int max_drift;
};

/* dvb_frontend_info matches the kernel; only `name` + caps + freq
 * limits + symbol-rate limits are touched by chip drivers. */
struct dvb_frontend_info {
    char       name[128];
    u32        frequency_min_hz;
    u32        frequency_max_hz;
    u32        frequency_stepsize_hz;
    u32        frequency_tolerance_hz;
    u32        symbol_rate_min;
    u32        symbol_rate_max;
    u32        symbol_rate_tolerance;
    u32        notifier_delay;
    u32        caps;
    /* delsys[] holds the supported delivery systems — chip-driver
     * source initializes a small array with `.delsys = {SYS_DVBT, …}`. */
    enum fe_delivery_system delsys[8];
};

/* analog_parameters — analog tuning parameters. Tuner drivers
 * (si2157) declare set_analog_params slots in their ops tables; the
 * code path is never exercised from our DVB-only stack but the
 * struct must be a complete type so the function signature compiles. */
struct analog_parameters {
    unsigned int frequency;
    unsigned int mode;
    unsigned int audmode;
    u64          std;
};

struct dvb_tuner_ops {
    struct dvb_frontend_info info;

    void (*release)(struct dvb_frontend *fe);
    int  (*init)(struct dvb_frontend *fe);
    int  (*sleep)(struct dvb_frontend *fe);
    int  (*suspend)(struct dvb_frontend *fe);
    int  (*resume)(struct dvb_frontend *fe);

    int  (*set_params)(struct dvb_frontend *fe);
    /* Analog set_params — never called from our stack but slot has
     * to exist so upstream tuner ops tables compile. */
    int  (*set_analog_params)(struct dvb_frontend *fe,
                              struct analog_parameters *p);
    int  (*get_frequency)(struct dvb_frontend *fe, u32 *frequency);
    int  (*get_if_frequency)(struct dvb_frontend *fe, u32 *frequency);
    int  (*get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);
    int  (*get_status)(struct dvb_frontend *fe, u32 *status);
    int  (*get_rf_strength)(struct dvb_frontend *fe, u16 *strength);
};

struct dvb_frontend_ops {
    struct dvb_frontend_info info;
    enum fe_delivery_system  delsys[8];

    void (*release)(struct dvb_frontend *fe);

    int  (*init)(struct dvb_frontend *fe);
    int  (*sleep)(struct dvb_frontend *fe);
    int  (*suspend)(struct dvb_frontend *fe);
    int  (*resume)(struct dvb_frontend *fe);

    int  (*set_frontend)(struct dvb_frontend *fe);
    int  (*get_frontend)(struct dvb_frontend *fe,
                         struct dtv_frontend_properties *p);

    int  (*read_status)(struct dvb_frontend *fe, enum fe_status *status);
    int  (*read_ber)   (struct dvb_frontend *fe, u32 *ber);
    int  (*read_signal_strength)(struct dvb_frontend *fe, u16 *strength);
    int  (*read_snr)   (struct dvb_frontend *fe, u16 *snr);
    int  (*read_ucblocks)(struct dvb_frontend *fe, u32 *ucblocks);

    int  (*get_tune_settings)(struct dvb_frontend *fe,
                              struct dvb_frontend_tune_settings *settings);

    int  (*tune)(struct dvb_frontend *fe, bool re_tune,
                 unsigned int mode_flags, unsigned int *delay,
                 enum fe_status *status);
    enum dvbfe_algo (*get_frontend_algo)(struct dvb_frontend *fe);

    int  (*ts_bus_ctrl)(struct dvb_frontend *fe, int acquire);

    /* i2c_gate_ctrl — older API for tuner-side i²c gating. The newer
     * pattern (si2168, lgdt3306a) uses i2c-mux instead, but the slot
     * has to exist because chip drivers populate it. */
    int  (*i2c_gate_ctrl)(struct dvb_frontend *fe, int enable);

    /* search-algo / get_frontend_algo for chip drivers (lgdt3306a)
     * that implement the dvbfe_algo + dvbfe_search return-value API. */
    enum dvbfe_search (*search)(struct dvb_frontend *fe);

    /* Inner tuner_ops — the tuner driver memcpys a const here from
     * its probe(). */
    struct dvb_tuner_ops tuner_ops;

    /* analog_ops, diseqc_*, … — out of scope. */
};

/* dvb_frontend — the per-frontend instance struct. demodulator_priv
 * and tuner_priv hold the i2c_client pointer the chip drivers use as
 * back-link to their dev struct. dtv_property_cache is the
 * parameter cache. */
struct dvb_frontend {
    struct dvb_frontend_ops          ops;
    struct dtv_frontend_properties   dtv_property_cache;
    void                            *demodulator_priv;
    void                            *tuner_priv;
    void                            *frontend_priv;
    int                              dvb_id;     /* unused — kept for size */
};

#endif /* LINUXDVBKPI_MEDIA_DVB_FRONTEND_H */
