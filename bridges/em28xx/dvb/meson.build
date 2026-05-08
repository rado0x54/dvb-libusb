# dvb_em28xx — DVB engine for em28xx-based boards. Owns the board
# table (USB IDs → which chips, GPIO sequence, frontend count), the
# i2c_new_client_device chip-attach flow, and the per-frontend
# usbq_stream + tune/read/status vtable.
#
# License: GPL-2.0-or-later for src/boards.c (transcribes upstream
# em28xx-dvb attach recipes); MIT for src/dvb_em28xx.c + headers
# (engine lifecycle, our code).

dvb_em28xx_inc = include_directories('include')

dvb_em28xx_lib = static_library(
  'dvb_em28xx',
  sources: files(
    'src/dvb_em28xx.c',
    'src/boards.c',
  ),
  include_directories: [
    dvb_em28xx_inc,
    include_directories('src'),
  ],
  dependencies: [
    em28xx_dep,
    chips_inc_dep,
    dvb_handle_dep,
    linuxdvbkpi_dep,
    usbq_dep,
  ],
)

dvb_em28xx_dep = declare_dependency(
  link_with: dvb_em28xx_lib,
  include_directories: dvb_em28xx_inc,
  dependencies: [
    em28xx_dep,
    chips_inc_dep,
    dvb_handle_dep,
    linuxdvbkpi_dep,
    usbq_dep,
  ],
)
