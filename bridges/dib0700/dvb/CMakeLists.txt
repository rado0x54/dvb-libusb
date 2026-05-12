# dvb_dib0700 — DVB engine for dib0700-based boards. Same shape as
# dvb_em28xx; dib0700-specific bits are firmware upload at
# cold-detect and the per-board GPIO bring-up.

add_library(dvb_dib0700 OBJECT
  src/dvb_dib0700.c
  src/boards.c
)

target_include_directories(dvb_dib0700 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(dvb_dib0700 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dvb_dib0700 PRIVATE
  dib0700
  chips
  dvb_handle
  linuxdvbkpi
  linuxdvbkpi_internal
  usbq
  dvb_libusb_project_flags
)
