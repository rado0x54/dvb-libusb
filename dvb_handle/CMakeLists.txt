# dvb_handle — header-only library defining the per-frontend handle
# struct + vtable + tune/status types shared between the bridge
# engines and consumers (libdvb_usb's tools / external apps).

add_library(dvb_handle INTERFACE)
target_include_directories(dvb_handle INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
