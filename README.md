# dvb-libusb

Userland driver framework for DVB USB devices, built on libusb. Lets
you bring up demodulator + tuner chips and capture the resulting MPEG-TS
stream **without a kernel `dvb-usb` build** — useful on macOS, FreeBSD,
or any host where the in-kernel DVB stack isn't an option.

The project lifts the chip drivers (demods, silicon tuners) verbatim
from upstream Linux at a pinned tag, compiled in userland against a
kernel-API polyfill (`linuxdvbkpi/`). Bridge protocol code (em28xx,
DiB0700) is hand-ported because the upstream framework it lives in
(dvb-usb-v2 / dvb-core) is too entangled with kernel-internal APIs to
lift cleanly. The result is roughly 1:3 polyfill-to-absorbed
upstream-LoC ratio, with new chip drivers slotting in for the cost of
adding a row to a board table.

The repo ships:

- **`libdvb_libusb`**, a single shared library wrapping the whole stack
  behind bridge-generic engine APIs.
- **`tools/dvb_devices`** — list supported boards and detect plugged-in
  ones.
- **`tools/dvb_pid_dump`** — tune to a frequency and dump the resulting
  TS stream (optionally PID-filtered) to stdout.

Not in this repo: any minisatip / SAT>IP plugin glue. That layer
lives separately and consumes `libdvb_libusb` like any other client.

## Supported devices

| Vendor / model                          | USB IDs              | Frontends | Delivery systems        | Tested |
|-----------------------------------------|----------------------|-----------|--------------------------|--------|
| Hauppauge WinTV-dualHD (DVB)            | `2040:0265`          | 2         | DVB-T, DVB-T2, DVB-C    | ✅ (incl. retune, multi-session) |
| Hauppauge WinTV-dualHD 01595 ATSC/QAM   | `2040:026d` `826d`–`8271` | 2  | ATSC, DVB-C ANNEX B     | ✅ (compile + plugin-load only — no live ATSC mux at hand) |
| Microsoft Xbox One Digital TV Tuner     | `045e:02d5`          | 1         | DVB-T, DVB-T2, DVB-C    | ✅ (incl. retune, multi-session) |

These three are the only boards currently in the engine board tables.
Everything else — including other em28xx-based DVB devices and other
dib0700 boards — should be a straightforward addition (one row + one
attach function per board), but is **untested**. Contributions
welcome; bring hardware.

Run `tools/dvb_devices` for the live list:

```sh
./build/tools/dvb_devices              # default: plugged-in devices (one entry per physical device)
./build/tools/dvb_devices --supported  # full board table this build can drive (no USB scan)
```

Detection is pure libusb enumeration — no device claim, no firmware
required. You'll see what's plugged in even before firmware is in
place. Two physical instances of the same board appear as two
entries with different bus/dev addresses.

## Building

The lifted upstream chip drivers (`si2168`, `si2157`, `mn88472`,
`tda18250`, `lgdt3306a`) aren't vendored. They're pulled on demand
from `torvalds/linux` at a pinned tag (see
`scripts/lifted-manifest.txt`) into a gitignored `chips/lifted/` directory.
Run the fetch script once before configuring CMake:

```sh
scripts/fetch-lifted.sh                        # default tag
LINUX_TAG=v6.14 scripts/fetch-lifted.sh        # override
```

Build with CMake:

```sh
cmake -S . -B build
cmake --build build -j
```

Configure errors out with a clear hint if `chips/lifted/` is empty.

### Build options

```sh
cmake -S . -B build -DDVB_LIBUSB_CHIP_DRIVER_VERBOSE=ON   # verbose chip dev_dbg
```

Heavy — produces hundreds of lines per tune. Useful when chasing chip-init
issues; off in normal operation.

### Runtime debug

```sh
DVB_DEBUG=1 ./build/tools/dvb_pid_dump ...
```

Engine-level logs: tune events, capture toggles, status reads,
URB/overflow stats. Zero overhead when the env var is unset.

## Firmware

Some boards need external firmware blobs that are NOT redistributed
here:

- WinTV-dualHD (DVB): `dvb-demod-si2168-b40-01.fw`
- Xbox One Tuner: `dvb-usb-dib0700-1.20.fw`, `dvb-demod-mn88472-02.fw`

Get them from the [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/)
tree (or your distro's `firmware-linux-nonfree` equivalent).

Lookup chain — same shape as the kernel's `request_firmware()`,
mirrored by `linuxdvbkpi`:

1. `linuxdvbkpi_set_firmware_root()` (programmatic override; library API)
2. `$FIRMWARE_DIR` (env override)
3. `/usr/local/lib/firmware`
4. `/usr/lib/firmware`
5. `/lib/firmware` (the kernel default)

Drop the blobs in any of those — the tools and library find them
automatically. No CLI arg needed unless you want to override.

## Running the tools

```sh
# 1. See what's supported and what's plugged in (no firmware needed):
./build/tools/dvb_devices

# 2. Tune a channel and dump TS packets to stdout (firmware must
#    be in /lib/firmware or one of the fallback paths, OR
#    $FIRMWARE_DIR set):
./build/tools/dvb_pid_dump dvbc 386000000 8000000 -1 30 > /tmp/cap.ts
# args: <delsys> <freq_hz> <bw_hz> <pid|-1=all> <duration_s> [idx]
# delsys: dvbt | dvbt2 | dvbc | atsc

# 3. Inspect the captured TS:
tsanalyze /tmp/cap.ts                          # tsduck
ffprobe /tmp/cap.ts                            # ffmpeg
```

## Library usage

`libdvb_libusb` is a single shared library. Consumers link it and
include the per-bridge DVB engine headers:

```c
#include <dvb_handle/dvb_handle.h>
#include <dvb_em28xx/dvb_em28xx.h>
#include <dvb_dib0700/dvb_dib0700.h>
#include <linuxdvbkpi/firmware_root.h>

linuxdvbkpi_set_firmware_root("/path/to/firmware");

dvb_frontend_handle_t *handles[8] = {0};
int n = 0;
n += dvb_em28xx_discover_all (&handles[n], 8 - n);
n += dvb_dib0700_discover_all(&handles[n], 8 - n);

for (int i = 0; i < n; i++) {
    dvb_frontend_handle_t *fe = handles[i];
    dvb_tune_params_t p = {
        .delsys       = SYS_DVBC_ANNEX_A,
        .freq_hz      = 386000000,
        .bandwidth_hz = 8000000,
        .symbol_rate  = 6900000,
        .stream_id    = -1,
    };
    fe->ops->tune(fe->engine_state, &p);

    uint8_t buf[48 * 1024];
    int got = fe->ops->read_ts(fe->engine_state, buf, sizeof(buf), 1000);
    /* ... */
}

dvb_dib0700_shutdown();
dvb_em28xx_shutdown();
```

CMake consumers can pull this repo in as a submodule (or
`FetchContent_Declare`) and `add_subdirectory(dvb-libusb)`, then
link the resulting target:

```cmake
add_subdirectory(third_party/dvb-libusb)
target_link_libraries(myapp PRIVATE dvb_libusb::dvb_libusb)
```

Tools (`dvb_devices`, `dvb_pid_dump`) and install rules default OFF
when consumed via `add_subdirectory`; flip with
`-DDVB_LIBUSB_BUILD_TOOLS=ON` / `-DDVB_LIBUSB_INSTALL=ON`.

## Limitations

- **macOS sleep/wake**: USB endpoints come back stale after the host
  sleeps mid-session; chip-side i²c still works, but the bulk-IN URBs
  return empty. Workaround: replug the device, or restart the
  process. Fix shape (deferred): libusb hotplug callbacks.
- **One device per VID:PID at open time**: `usbq_open()` claims the
  first match. `dvb_devices` enumerates every plugged-in physical
  device (one entry per bus/address), but the per-frontend `open()`
  path doesn't yet take a bus/address selector — opening two of the
  same board concurrently currently needs `usbq_open()` to grow that
  selector.
- **Single backend**: libusb only. iousbhost was prototyped in a
  predecessor and didn't reduce the discontinuity floor on macOS;
  the abstraction was dropped here for simplicity.

## Architecture in 30 seconds

```
┌────────────────────────────────────────────────────────────────┐
│ tools/  +  third-party consumers (your SAT>IP plugin, app, …)   │
└─────────────────────────────┬───────────────────────────────────┘
                              │ libdvb_libusb public API
                              │ (dvb_em28xx_* / dvb_dib0700_*)
┌─────────────────────────────┴───────────────────────────────────┐
│ bridges/em28xx/dvb        │  bridges/dib0700/dvb                 │
│ (DVB engine: board table, │  (DVB engine: board table,           │
│  per-frontend lifecycle,  │   per-frontend lifecycle, vtable)    │
│  vtable)                  │                                       │
│  · WinTV-dualHD DVB       │  · Xbox One Tuner                    │
│  · WinTV-dualHD ATSC      │                                       │
└──────┬───────────────────┴────────┬─────────────────────────────┘
       │ em28xx_* (protocol)         │ dib0700_* (protocol)
┌──────┴────────────────┐  ┌─────────┴───────────┐
│ bridges/em28xx/bridge │  │ bridges/dib0700/    │  ← manual ports of
│ (USB protocol: vendor │  │   bridge            │    the bridge protocol
│  control, i²c, GPIO,  │  │ (USB protocol)      │    (USB control + i²c)
│  TS-bus enable)       │  │                     │
└──────┬────────────────┘  └─────────┬───────────┘
       │                              │
       └─── via i²c → ────────────────┴──→  chips/  (lifted upstream:
                                              si2168, si2157, lgdt3306a,
                                              mn88472, tda18250)

   ┌──────────────────────────────────────────────────────────────┐
   │ linuxdvbkpi — Linux kernel-API polyfill                       │
   │ (compiled-against by the lifted chip drivers)                 │
   └──────────────────────────────────────────────────────────────┘
                                  │
   ┌──────────────────────────────┴────────────────────────────────┐
   │ usbq — libusb wrapper (sync + async stream)                    │
   └──────────────────────────────────────────────────────────────┘
```

## License

The repository is a combined work; **per-file `SPDX-License-Identifier`
headers are authoritative**. At a high level:

- **MIT** — clean-room code with no upstream-Linux derivation:
  `linuxdvbkpi/`, `usbq/`, `dvb_handle/`, the per-bridge engine
  lifecycle (`bridges/em28xx/dvb/src/dvb_em28xx.c` + headers,
  `bridges/dib0700/dvb/src/dvb_dib0700.c` + headers), `tools/`, and
  the build helpers in `scripts/`.

- **GPL-2.0-or-later** — derivative of upstream Linux media drivers:
  the bridge ports (`bridges/em28xx/bridge/`, `bridges/dib0700/bridge/`)
  and the per-board attach recipes (`bridges/em28xx/dvb/src/boards.c`,
  `bridges/dib0700/dvb/src/boards.c`,
  which transcribe upstream `em28xx-dvb.c` / `dib0700_devices.c`
  init functions).

- **GPL-2.0-or-later** — lifted verbatim from upstream Linux: the
  files that `scripts/fetch-lifted.sh` pulls into `chips/lifted/`. License
  matches each upstream file's SPDX header. Not vendored here.

The combined static library `libdvb_libusb` and the binaries built
from `tools/` link MIT and GPL code together. MIT is
[GPL-compatible per the FSF](https://www.gnu.org/licenses/license-list.html#Expat),
so the combined work distributes under GPL-2.0-or-later (see
`LICENSE`). The MIT-licensed sources keep their MIT terms in source
form — fork the polyfill or the usbq wrapper and reuse them under
MIT in a non-GPL context if you want; the only restriction is that
you can't redistribute them combined with the GPL parts under
non-GPL terms.

`LICENSES/` holds the canonical text of each license used.
