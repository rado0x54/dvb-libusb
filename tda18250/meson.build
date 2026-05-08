# tda18250 — NXP silicon tuner, lifted verbatim. See LIFTED_FROM.
# License: GPL-2.0-or-later.

lifted_dir = meson.project_source_root() / 'lifted' / 'tda18250'
tda18250_inc = include_directories('../lifted')

tda18250_lib = static_library(
  'tda18250',
  sources: [lifted_dir / 'tda18250.c'],
  include_directories: [
    tda18250_inc,
    include_directories('../lifted/tda18250'),
  ],
  dependencies: [linuxdvbkpi_dep],
  c_args: chip_lift_c_args,
)

tda18250_dep = declare_dependency(
  link_whole: tda18250_lib,
  include_directories: tda18250_inc,
  dependencies: [linuxdvbkpi_dep],
)
