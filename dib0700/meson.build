# dib0700 — userland port of the DiBcom DiB0700 USB-DVB bridge driver.
#
# Manually ported from drivers/media/usb/dvb-usb/dib0700_core.c (the
# protocol implementation), in the same shape as our em28xx/ port.
# Lifting upstream verbatim is impractical because the bridge driver
# tangles into the kernel's dvb-usb framework, URB infrastructure, and
# the input subsystem — none of which we mirror in userland.
#
# The chip drivers downstream (mn88472, tda18250) ARE lifted verbatim
# via linuxdvbkpi/.
#
# License: GPL-2.0-or-later (same as the upstream protocol it derives
# from; combined work that includes this lib is GPL-2.0+).

dib0700_inc = include_directories('include')

dib0700_lib = static_library(
  'dib0700',
  sources: [
    'src/dib0700_core.c',
    'src/dib0700_i2c.c',
    'src/dib0700_fw.c',
    'src/dib0700_stream.c',
  ],
  include_directories: [dib0700_inc, include_directories('src')],
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)

dib0700_dep = declare_dependency(
  link_with: dib0700_lib,
  include_directories: dib0700_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)
