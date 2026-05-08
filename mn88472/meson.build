# mn88472 — Panasonic DVB-T/T2/C demod, lifted verbatim. See LIFTED_FROM.
# License: GPL-2.0-or-later.

lifted_dir = meson.project_source_root() / 'lifted' / 'mn88472'
mn88472_inc = include_directories('../lifted')

mn88472_lib = static_library(
  'mn88472',
  sources: [lifted_dir / 'mn88472.c'],
  include_directories: [
    mn88472_inc,
    include_directories('../lifted/mn88472'),
  ],
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,
)

mn88472_dep = declare_dependency(
  link_whole: mn88472_lib,
  include_directories: mn88472_inc,
  dependencies: [linuxdvbkpi_dep],
)
