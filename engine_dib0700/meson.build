# engine_dib0700 — bridge-generic engine for dib0700-based DVB USB
# devices. Same architecture as engine_em28xx (board table inside,
# scans USB for matches, runs board-specific GPIO bring-up + chip
# attach via i2c_new_client_device).
#
# License: GPL-2.0-or-later (combined work with the lifted upstream
# chip drivers it pulls in: mn88472, tda18250).

engine_dib0700_inc = include_directories('include')

engine_dib0700_lib = static_library(
  'engine_dib0700',
  sources: [
    'src/engine_dib0700.c',
    'src/boards.c',
  ],
  include_directories: [engine_dib0700_inc, include_directories('src')],
  dependencies: [
    dvb_handle_dep,
    dib0700_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    mn88472_dep,
    tda18250_dep,
  ],
)

engine_dib0700_dep = declare_dependency(
  link_with: engine_dib0700_lib,
  include_directories: engine_dib0700_inc,
  dependencies: [
    dvb_handle_dep,
    dib0700_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    mn88472_dep,
    tda18250_dep,
  ],
)
