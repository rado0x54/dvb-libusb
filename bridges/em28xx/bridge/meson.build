# em28xx — Empia em28xx USB-bridge protocol port. Knows how to talk
# vendor-control + i²c-over-USB + GPIO + TS-bus enable to the bridge
# chip; knows nothing about DVB demods or tuners.
#
# License: GPL-2.0-or-later (manual port of upstream
# drivers/media/usb/em28xx/em28xx-{core,i2c,cards,dvb}.c).

em28xx_inc = include_directories('include')

em28xx_lib = static_library(
  'em28xx',
  sources: files(
    'src/em28xx_core.c',
    'src/em28xx_i2c.c',
    'src/em28xx_gpio.c',
    'src/em28xx_ts.c',
  ),
  include_directories: em28xx_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)

em28xx_dep = declare_dependency(
  link_with: em28xx_lib,
  include_directories: em28xx_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)
