project(
  'dvb_libusb', ['c'],
  version: '0.1.0',
  license: 'GPL-2.0-or-later',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'b_ndebug=if-release',
  ],
  meson_version: '>=0.60.0',
)

cc = meson.get_compiler('c')

# Strict warnings on top of warning_level=2 — applied only to our own
# code. Lifted upstream chip drivers use the relaxed
# `chip_lift_c_args` set defined below.
project_cflags = cc.get_supported_arguments([
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wshadow',
  '-Wpointer-arith',
  '-Wno-unused-parameter',
])
add_project_arguments(project_cflags, language: 'c')

libusb_dep = dependency('libusb-1.0', version: '>=1.0.20', required: true)

# ---- Lifted-source presence check ------------------------------- *
#
# scripts/fetch-lifted.sh pulls upstream chip-driver sources at the
# pinned LINUX_TAG into chips/lifted/. The chips/ subdir's own
# meson.build re-checks per-file and references them; here we just
# fail configure early with a friendly message if the user hasn't
# run the fetch step yet.

fs = import('fs')
if not fs.exists(meson.project_source_root() / 'chips' / 'lifted' / 'si2168' / 'si2168.c')
  error(
    'Lifted upstream chip drivers not present (chips/lifted/ is empty).\n' +
    'Run scripts/fetch-lifted.sh before configuring meson. See README.'
  )
endif

# Common compile flags for every lifted-upstream chip-driver lib.
# Chip drivers don't compile clean under our strict warning set; we
# relax those just for them. The `-include linuxdvbkpi/linuxdvbkpi.h`
# force-includes the polyfill umbrella so upstream's
# `#include <linux/i2c.h>` etc. resolves to OUR headers.
chip_lift_c_args = [
  '-include', 'linuxdvbkpi/linuxdvbkpi.h',
  '-Wno-unused-but-set-variable',
  '-Wno-unused-variable',
  '-Wno-unused-function',
  '-Wno-missing-prototypes',
  '-Wno-strict-prototypes',
  '-Wno-shadow',
  '-Wno-pointer-arith',
  '-Wno-sign-compare',
  '-Wno-pointer-sign',
  '-Wno-cast-qual',
  '-Wno-unused-parameter',
]

# ---- Component static libs (leaf-first) -------------------------- *

subdir('dvb_handle')      # header-only types
subdir('linuxdvbkpi')     # kernel-API polyfill
subdir('usbq')            # libusb wrapper
subdir('chips')           # all 5 lifted upstream chip drivers
subdir('bridges')         # bridge protocol + DVB engine, per family

# ---- Top-level shared library ------------------------------------ *
#
# `libdvb_libusb` is the only artifact a downstream consumer links
# against. It link_whole's the chip drivers (their only outward
# footprint is the constructor emitted by module_i2c_driver — would
# be discarded under link_with) plus every per-family DVB engine
# (so the engines' board tables are present in the symbol map for
# discover_all to walk). Everything else is link_with.

libdvb_libusb = shared_library(
  'dvb_libusb',
  link_whole: [
    chips_lib,
    dvb_em28xx_lib,
    dvb_dib0700_lib,
  ],
  dependencies: [
    em28xx_dep,
    dib0700_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    dvb_handle_dep,
  ],
  install: true,
  soversion: '0',
  version:   meson.project_version(),
)

# Single dep for downstream consumers (tools, third-party apps).
dvb_libusb_dep = declare_dependency(
  link_with: libdvb_libusb,
  dependencies: [
    dvb_handle_dep,
    dvb_em28xx_dep,
    dvb_dib0700_dep,
    linuxdvbkpi_dep,   # for linuxdvbkpi_set_firmware_root
  ],
)

subdir('tools')

summary({
  'libusb-1.0':           libusb_dep.version(),
  'compiler':             cc.get_id() + ' ' + cc.version(),
  'chip_driver_verbose':  get_option('chip_driver_verbose'),
}, section: 'Toolchain')
