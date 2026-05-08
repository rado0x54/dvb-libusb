# chips — all lifted upstream Linux DVB chip drivers in one library.
# Source files live under lifted/ (gitignored); populated by
# scripts/fetch-lifted.sh from torvalds/linux at the pinned tag.
#
# Five chips today:
#   si2168    DVB-T/T2/C demod        (WinTV-dualHD DVB)
#   si2157    silicon tuner           (both WinTV-dualHD variants)
#   lgdt3306a ATSC/QAM demod          (WinTV-dualHD 01595 ATSC)
#   mn88472   DVB-T/T2/C demod        (Xbox One Tuner)
#   tda18250  silicon tuner           (Xbox One Tuner)
#
# License: GPL-2.0-or-later (each upstream file's SPDX header).

# Verify the lifted/ tree is populated. Top-level meson.build also
# checks; this is a per-component error message tying users to the
# fetch step.
fs = import('fs')
required_chip_sources = [
  'lifted/si2168/si2168.c',
  'lifted/si2157/si2157.c',
  'lifted/mn88472/mn88472.c',
  'lifted/tda18250/tda18250.c',
  'lifted/lgdt3306a/lgdt3306a.c',
]
foreach f : required_chip_sources
  if not fs.exists(meson.current_source_dir() / f)
    error(
      'Lifted chip driver missing: chips/@0@.\n'.format(f) +
      'Run scripts/fetch-lifted.sh before configuring meson.'
    )
  endif
endforeach

# Each chip has a private header with the same basename as the .c
# (e.g. si2168.c includes "si2168.h" from its own dir). We list every
# per-chip dir as an include path so those resolve.
chips_inc = include_directories('lifted')
chip_internal_includes = [
  chips_inc,
  include_directories('lifted/si2168'),
  include_directories('lifted/si2157'),
  include_directories('lifted/mn88472'),
  include_directories('lifted/tda18250'),
  include_directories('lifted/lgdt3306a'),
]

chips_lib = static_library(
  'chips',
  sources: [
    'lifted/si2168/si2168.c',
    'lifted/si2157/si2157.c',
    'lifted/mn88472/mn88472.c',
    'lifted/tda18250/tda18250.c',
    'lifted/lgdt3306a/lgdt3306a.c',
  ],
  include_directories: chip_internal_includes,
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,
)

# Two deps published:
#
#   chips_inc_dep   — headers only. Engines depend on this so they
#                     can `#include "si2168/si2168.h"` etc. without
#                     pulling chip object files into their archive
#                     (which would cause multi-link duplicate symbols
#                     when libdvb_usb link_whole's the chip lib).
#
#   chips_lib       — the static library itself. The top-level
#                     libdvb_usb meson.build link_whole's it exactly
#                     once. Don't expose this via a dep that could
#                     be pulled transitively.
chips_inc_dep = declare_dependency(
  include_directories: chips_inc,
  dependencies: [linuxdvbkpi_dep],
)
