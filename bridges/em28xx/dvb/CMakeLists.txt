# dvb_em28xx — DVB engine for em28xx-based boards. Owns the board
# table (USB IDs → which chips, GPIO sequence, frontend count), the
# i2c_new_client_device chip-attach flow, and the per-frontend
# usbq_stream + tune/read/status vtable.
#
# License: GPL-2.0-or-later.

# OBJECT lib — the board table is only reached via discover_all
# walking the symbol map of the final shared lib. Plain static
# archive would let the linker drop unreferenced TUs.
add_library(dvb_em28xx OBJECT
  src/dvb_em28xx.c
  src/boards.c
)

target_include_directories(dvb_em28xx PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(dvb_em28xx PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dvb_em28xx PRIVATE
  em28xx
  chips                     # for headers under chips/lifted/<chip>/
  dvb_handle
  linuxdvbkpi
  linuxdvbkpi_internal
  usbq
  dvb_libusb_project_flags
)
