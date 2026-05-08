project(
  'dvb_usb', ['c'],
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

# Strict warnings on top of warning_level=2 — but only on our own
# code. Lifted upstream chip drivers use a relaxed set defined as
# `chip_lift_c_args` below.
project_cflags = cc.get_supported_arguments([
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wshadow',
  '-Wpointer-arith',
  '-Wno-unused-parameter',
])
add_project_arguments(project_cflags, language: 'c')

# Single non-system dependency: libusb 1.0.
libusb_dep = dependency('libusb-1.0', version: '>=1.0.20', required: true)

# ---- Lifted chip-driver source presence check -------------------- *
#
# We don't vendor the upstream Linux chip drivers in this repo —
# scripts/fetch-lifted.sh pulls them into a gitignored `lifted/` dir
# at the pinned LINUX_TAG. If the user hasn't run it yet, fail
# configure with a clear hint instead of letting compile errors
# bubble up later.

fs = import('fs')
required_lifted = [
  'lifted/si2168/si2168.c',
  'lifted/si2157/si2157.c',
  'lifted/mn88472/mn88472.c',
  'lifted/tda18250/tda18250.c',
  'lifted/lgdt3306a/lgdt3306a.c',
]
foreach path : required_lifted
  if not fs.exists(meson.project_source_root() / path)
    error(
      'Lifted upstream chip driver not found: @0@.\n'.format(path) +
      'Run scripts/fetch-lifted.sh to populate the lifted/ directory ' +
      'before configuring meson. See README "Building" for details.'
    )
  endif
endforeach

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

# ---- Layers, leaf-first ------------------------------------------ *

subdir('dvb_handle')      # Header-only: handle/vtable types
subdir('linuxdvbkpi')     # Linux kernel-API polyfill
subdir('usbq')            # libusb wrapper
subdir('em28xx')          # em28xx USB bridge port (manual)
subdir('dib0700')         # DiB0700 USB bridge port (manual)

# Lifted upstream chip drivers (link_whole'd into the engines):
subdir('si2168')
subdir('si2157')
subdir('mn88472')
subdir('tda18250')
subdir('lgdt3306a')

# Bridge-generic engines orchestrating bridge + chip drivers:
subdir('engine_em28xx')
subdir('engine_dib0700')

# ---- Bundled library --------------------------------------------- *
#
# `libdvb_usb` is a single static library that consumers link
# against. It link_whole's the engines (which in turn link_whole the
# chip drivers — needed for module_i2c_driver constructors), and
# pulls in usbq + linuxdvbkpi + the bridge ports as link_with.

dvb_usb_lib = static_library(
  'dvb_usb',
  link_whole: [
    engine_em28xx_lib,
    engine_dib0700_lib,
    si2168_lib,
    si2157_lib,
    mn88472_lib,
    tda18250_lib,
    lgdt3306a_lib,
  ],
  dependencies: [
    em28xx_dep,
    dib0700_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    dvb_handle_dep,
  ],
)

dvb_usb_dep = declare_dependency(
  link_with: dvb_usb_lib,
  dependencies: [
    dvb_handle_dep,
    engine_em28xx_dep,
    engine_dib0700_dep,
    linuxdvbkpi_dep,
    em28xx_dep,
    dib0700_dep,
    usbq_dep,
    libusb_dep,
  ],
)

subdir('tools')

summary({
  'libusb-1.0':           libusb_dep.version(),
  'compiler':             cc.get_id() + ' ' + cc.version(),
  'chip_driver_verbose':  get_option('chip_driver_verbose'),
}, section: 'Toolchain')
