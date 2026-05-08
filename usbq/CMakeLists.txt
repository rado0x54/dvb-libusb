# usbq — libusb wrapper providing the USB-IO surface every chip-driver
# layer above it consumes (em28xx, dib0700, the engines, the tools).
#
# License: GPL-2.0-or-later (combined work).

add_library(usbq STATIC src/usbq.c)

target_include_directories(usbq PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(usbq
  PUBLIC  PkgConfig::libusb
  PRIVATE dvb_libusb_project_flags
)
