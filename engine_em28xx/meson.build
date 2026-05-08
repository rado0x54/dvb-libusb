# engine_em28xx — bridge-generic engine for em28xx-based DVB USB
# devices. Drives the em28xx bridge protocol + frontend lifecycle;
# board-specific knowledge (chip choices, addrs, GPIO sequences)
# lives in src/boards.c as a static board table.
#
# License: GPL-2.0-or-later (combined work with the lifted upstream
# chip drivers it pulls in).

engine_em28xx_inc = include_directories('include')

engine_em28xx_lib = static_library(
  'engine_em28xx',
  sources: [
    'src/engine_em28xx.c',
    'src/boards.c',
  ],
  include_directories: [engine_em28xx_inc, include_directories('src')],
  dependencies: [
    dvb_handle_dep,
    em28xx_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    si2168_dep,
    si2157_dep,
    lgdt3306a_dep,
  ],
)

engine_em28xx_dep = declare_dependency(
  link_with: engine_em28xx_lib,
  include_directories: engine_em28xx_inc,
  dependencies: [
    dvb_handle_dep,
    em28xx_dep,
    usbq_dep,
    linuxdvbkpi_dep,
    si2168_dep,
    si2157_dep,
    lgdt3306a_dep,
  ],
)
