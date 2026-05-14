# dvb_hotplug — libusb hotplug surface for dvb_libusb consumers.
#
# OBJECT lib (same shape as usbq) so its .o folds into the top-level
# dvb_libusb archive without becoming a separately-distributable
# transitive target. The public header at include/dvb_hotplug/ is
# re-exported through dvb_libusb's INTERFACE include dirs.

add_library(dvb_hotplug OBJECT src/dvb_hotplug.c)

target_include_directories(dvb_hotplug PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(dvb_hotplug
  PUBLIC  PkgConfig::libusb
          dvb_handle
  PRIVATE dvb_libusb_project_flags
)

# Needs the three bridge engines' public headers to call their
# *_supported_boards() at runtime for the VID:PID filter.
target_include_directories(dvb_hotplug PRIVATE
  ${PROJECT_SOURCE_DIR}/bridges/em28xx/dvb/include
  ${PROJECT_SOURCE_DIR}/bridges/dib0700/dvb/include
  ${PROJECT_SOURCE_DIR}/bridges/dvbsky/dvb/include
  ${PROJECT_SOURCE_DIR}/usbq/include
)
