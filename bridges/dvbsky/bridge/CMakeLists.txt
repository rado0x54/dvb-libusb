# dvbsky — DVBSky/MyGica/EyeTV USB-bridge protocol port. The bridge
# speaks a tiny opcode protocol over a pair of bulk endpoints
# (0x01 OUT cmd, 0x81 IN response, 0x82 IN TS) — no vendor control
# transfers, no firmware upload. I²c-over-USB, GPIO, streaming
# enable/disable. No DVB knowledge.
#
# License: GPL-2.0-or-later (manual port of upstream
# drivers/media/usb/dvb-usb-v2/dvbsky.c).

# OBJECT lib — see em28xx/CMakeLists.txt for rationale.
add_library(dvbsky OBJECT
  src/dvbsky_core.c
  src/dvbsky_i2c.c
  src/dvbsky_stream.c
)

target_include_directories(dvbsky PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(dvbsky
  PUBLIC  usbq
  PRIVATE linuxdvbkpi linuxdvbkpi_internal dvb_libusb_project_flags
)
