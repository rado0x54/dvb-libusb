# lgdt3306a — LG Electronics ATSC/QAM demod, lifted verbatim.
# See LIFTED_FROM. License: GPL-2.0-or-later.

lifted_dir = meson.project_source_root() / 'lifted' / 'lgdt3306a'
lgdt3306a_inc = include_directories('../lifted')

lgdt3306a_lib = static_library(
  'lgdt3306a',
  sources: [lifted_dir / 'lgdt3306a.c'],
  include_directories: [
    lgdt3306a_inc,
    include_directories('../lifted/lgdt3306a'),
  ],
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,
)

lgdt3306a_dep = declare_dependency(
  link_whole: lgdt3306a_lib,
  include_directories: lgdt3306a_inc,
  dependencies: [linuxdvbkpi_dep],
)
