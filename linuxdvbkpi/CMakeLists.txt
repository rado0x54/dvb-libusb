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

# Public surface (PUBLIC): only the consumer-facing API
# headers — currently just <linuxdvbkpi/firmware_root.h>. Anything
# put here transitively propagates to downstream consumers like
# minisatip.
#
# Internal surface (PRIVATE): the kernel-API polyfill — <linux/...>,
# <asm/...>, <media/...>, and the linuxdvbkpi.h umbrella that chip
# drivers force-include. These shadow the consumer's system headers
# if PUBLIC-exported (the polyfill <linux/dvb/frontend.h> intercepts
# minisatip's #include <linux/dvb/frontend.h>, the <linux/errno.h>
# polyfill defeats glibc's errno chain, etc.). Keeping the polyfill
# directory PRIVATE means it's only on the compile path while
# building linuxdvbkpi's own sources.
#
# In-tree consumers that DO need the polyfill (chip drivers, bridge
# engines) link against the linuxdvbkpi_internal INTERFACE target
# below instead — that one PRIVATELY propagates the polyfill include
# dir, so it never reaches dvb_libusb's INTERFACE.
target_include_directories(linuxdvbkpi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

add_library(linuxdvbkpi_internal INTERFACE)
target_include_directories(linuxdvbkpi_internal INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# BUILD_INTERFACE wrap: dvb_libusb_project_flags is an internal
# compile-flag INTERFACE target. Static archives normally export
# their PRIVATE link deps so consumers can complete the link, but
# those flags carry no symbols — and project_flags isn't part of
# the install/export set. The genex strips it on export.
target_link_libraries(linuxdvbkpi PRIVATE
  $<BUILD_INTERFACE:dvb_libusb_project_flags>
)

# Compile-time verbosity flag for the lifted chip drivers' dev_dbg /
# pr_debug output. Off by default; flip with
#   cmake -DDVB_LIBUSB_CHIP_DRIVER_VERBOSE=ON
# Heavy: hundreds of lines per tune. PUBLIC so the chip driver TUs
# (which include linuxdvbkpi.h via -include) see the same flag.
if(DVB_LIBUSB_CHIP_DRIVER_VERBOSE)
  target_compile_definitions(linuxdvbkpi PUBLIC LINUXDVBKPI_VERBOSE=1)
endif()
