# dvb_dvbsky — DVB engine for dvbsky-based boards (Mygica T230 family,
# Geniatech EyeTV Stick, DVBSky-branded sticks). Same shape as
# dvb_dib0700 / dvb_em28xx — board table, per-board GPIO bring-up,
# per-board chip attach (si2168 demod + si2141/si2157 tuner). No
# bridge firmware upload (the chip ships warm).

add_library(dvb_dvbsky OBJECT
  src/dvb_dvbsky.c
  src/boards.c
)

target_include_directories(dvb_dvbsky PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(dvb_dvbsky PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dvb_dvbsky PRIVATE
  dvbsky
  chips
  dvb_handle
  linuxdvbkpi
  linuxdvbkpi_internal
  usbq
  dvb_libusb_project_flags
)
