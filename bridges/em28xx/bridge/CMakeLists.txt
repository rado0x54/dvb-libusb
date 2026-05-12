# em28xx — Empia em28xx USB-bridge protocol port. Knows how to talk
# vendor-control + i²c-over-USB + GPIO + TS-bus enable to the bridge
# chip; knows nothing about DVB demods or tuners.
#
# License: GPL-2.0-or-later (manual port of upstream
# drivers/media/usb/em28xx/em28xx-{core,i2c,cards,dvb}.c).

# OBJECT lib (rather than STATIC) so its .o's fold into dvb_libusb
# directly — works for both shared and static dvb_libusb output
# without needing em28xx in the install/export set.
add_library(em28xx OBJECT
  src/em28xx_core.c
  src/em28xx_i2c.c
  src/em28xx_gpio.c
  src/em28xx_ts.c
)

target_include_directories(em28xx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(em28xx
  PUBLIC  usbq
  PRIVATE linuxdvbkpi linuxdvbkpi_internal dvb_libusb_project_flags
)
