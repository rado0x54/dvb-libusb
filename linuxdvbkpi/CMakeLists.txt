# linuxdvbkpi — Linux kernel-API polyfill for userspace chip-driver
# lift. Lets us compile drivers/media/dvb-frontends/*.c and
# drivers/media/tuners/*.c verbatim from upstream Linux without any
# source modifications.
#
# License: GPL-2.0-or-later (combined work that links the lifted
# upstream chip drivers).

add_library(linuxdvbkpi STATIC
  src/i2c.c
  src/i2c_mux.c
  src/regmap.c
  src/firmware.c
  src/intlog.c
)

target_include_directories(linuxdvbkpi PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(linuxdvbkpi PRIVATE dvb_libusb_project_flags)

# Compile-time verbosity flag for the lifted chip drivers' dev_dbg /
# pr_debug output. Off by default; flip with
#   cmake -DDVB_LIBUSB_CHIP_DRIVER_VERBOSE=ON
# Heavy: hundreds of lines per tune. PUBLIC so the chip driver TUs
# (which include linuxdvbkpi.h via -include) see the same flag.
if(DVB_LIBUSB_CHIP_DRIVER_VERBOSE)
  target_compile_definitions(linuxdvbkpi PUBLIC LINUXDVBKPI_VERBOSE=1)
endif()
