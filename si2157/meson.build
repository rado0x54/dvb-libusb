# si2157 — Silicon Labs silicon tuner, lifted verbatim. See LIFTED_FROM.
# License: GPL-2.0-or-later.

lifted_dir = meson.project_source_root() / 'lifted' / 'si2157'
si2157_inc = include_directories('../lifted')

si2157_lib = static_library(
  'si2157',
  sources: [lifted_dir / 'si2157.c'],
  include_directories: [
    si2157_inc,
    include_directories('../lifted/si2157'),
  ],
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,
)

si2157_dep = declare_dependency(
  link_whole: si2157_lib,
  include_directories: si2157_inc,
  dependencies: [linuxdvbkpi_dep],
)
