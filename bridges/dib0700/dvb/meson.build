# dvb_dib0700 — DVB engine for dib0700-based boards. Same shape as
# dvb_em28xx; dib0700-specific bits are firmware upload at
# cold-detect and the per-board GPIO bring-up.

dvb_dib0700_inc = include_directories('include')

dvb_dib0700_lib = static_library(
  'dvb_dib0700',
  sources: files(
    'src/dvb_dib0700.c',
    'src/boards.c',
  ),
  include_directories: [
    dvb_dib0700_inc,
    include_directories('src'),
  ],
  dependencies: [
    dib0700_dep,
    chips_inc_dep,
    dvb_handle_dep,
    linuxdvbkpi_dep,
    usbq_dep,
  ],
)

dvb_dib0700_dep = declare_dependency(
  link_with: dvb_dib0700_lib,
  include_directories: dvb_dib0700_inc,
  dependencies: [
    dib0700_dep,
    chips_inc_dep,
    dvb_handle_dep,
    linuxdvbkpi_dep,
    usbq_dep,
  ],
)
