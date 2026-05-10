# usbq — libusb wrapper providing the USB-IO surface every chip-driver
# layer above it consumes (em28xx, dib0700, the engines, the tools).
#
# License: GPL-2.0-or-later (combined work).

# OBJECT lib (rather than STATIC) so when dvb_libusb is built as a
# static archive, usbq's .o folds in directly. With STATIC, CMake
# would require usbq to be in the export set (consumers of a static
# dvb_libusb.a would otherwise see an undefined transitive
# dependency on usbq.a). usbq is an internal layer.
add_library(usbq OBJECT src/usbq.c)

target_include_directories(usbq PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(usbq
  PUBLIC  PkgConfig::libusb
  PRIVATE dvb_libusb_project_flags
)
