# si2168 — Silicon Labs DVB-T/T2/C demodulator, lifted verbatim from
# upstream Linux. Source files fetched into ../lifted/si2168/ by
# scripts/fetch-lifted.sh; meson errors out at configure time if they
# aren't there yet (see top-level meson.build).
#
# License: GPL-2.0-or-later (upstream's; combined work is GPL-2.0+).

lifted_dir = meson.project_source_root() / 'lifted' / 'si2168'
si2168_inc = include_directories('../lifted')   # so consumers can do
                                                 # #include "si2168/si2168.h"

si2168_lib = static_library(
  'si2168',
  sources: [lifted_dir / 'si2168.c'],
  include_directories: [
    si2168_inc,
    include_directories('../lifted/si2168'),     # for the chip's own
                                                  # `#include "si2168.h"`
  ],
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,                      # defined at top-level
)

# `link_whole` (not `link_with`): every symbol in the lifted source
# is static; the only outward-visible footprint is the constructor
# emitted by module_i2c_driver(si2168_driver). With link_with the .o
# is dropped (no referenced symbol), and the constructor never runs.
si2168_dep = declare_dependency(
  link_whole: si2168_lib,
  include_directories: si2168_inc,
  dependencies: [linuxdvbkpi_dep],
)
