# dib0700 — DiBcom DiB0700 USB-bridge protocol port. Vendor control
# transfers, i²c-over-USB, firmware upload (Intel HEX over bulk-OUT),
# GPIO, streaming_ctrl. No DVB knowledge.
#
# License: GPL-2.0-or-later (manual port of upstream
# drivers/media/usb/dvb-usb/dib0700_*.c).

# OBJECT lib — see em28xx/CMakeLists.txt for rationale.
add_library(dib0700 OBJECT
  src/dib0700_core.c
  src/dib0700_i2c.c
  src/dib0700_fw.c
  src/dib0700_stream.c
)

target_include_directories(dib0700 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(dib0700
  PUBLIC  usbq
  PRIVATE linuxdvbkpi linuxdvbkpi_internal dvb_libusb_project_flags
)
