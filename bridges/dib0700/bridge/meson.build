# dib0700 — DiBcom DiB0700 USB-bridge protocol port. Vendor control
# transfers, i²c-over-USB, firmware upload (Intel HEX over bulk-OUT),
# GPIO, streaming_ctrl. No DVB knowledge.
#
# License: GPL-2.0-or-later (manual port of upstream
# drivers/media/usb/dvb-usb/dib0700_*.c).

dib0700_inc = include_directories('include')

dib0700_lib = static_library(
  'dib0700',
  sources: files(
    'src/dib0700_core.c',
    'src/dib0700_i2c.c',
    'src/dib0700_fw.c',
    'src/dib0700_stream.c',
  ),
  include_directories: dib0700_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)

dib0700_dep = declare_dependency(
  link_with: dib0700_lib,
  include_directories: dib0700_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)
