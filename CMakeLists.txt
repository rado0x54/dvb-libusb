cmake_minimum_required(VERSION 3.16)

# CMake 3.29+ — link-line dedup that drops repeated static archives
# pulled in via both PUBLIC and transitive-PRIVATE paths. Without
# this, Apple ld emits a benign "ignoring duplicate libraries"
# warning for liblinuxdvbkpi.a.
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

project(dvb_libusb
  VERSION 0.1.0
  LANGUAGES C
  DESCRIPTION "Userspace libusb-based DVB driver bundle"
)

# ---- Top-level vs. submodule detection -------------------------------- *
#
# When this project is pulled in via add_subdirectory() the consumer
# usually does not want our tools, install rules, or test scaffolding
# unless they opt in. PROJECT_IS_TOP_LEVEL needs CMake 3.21+; fall back
# to the manual check for older toolchains.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
  if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(PROJECT_IS_TOP_LEVEL ON)
  else()
    set(PROJECT_IS_TOP_LEVEL OFF)
  endif()
endif()

option(DVB_LIBUSB_BUILD_TOOLS
  "Build dvb_devices / dvb_pid_dump CLI tools"
  ${PROJECT_IS_TOP_LEVEL})
option(DVB_LIBUSB_INSTALL
  "Generate install rules for dvb_libusb"
  ${PROJECT_IS_TOP_LEVEL})
option(DVB_LIBUSB_CHIP_DRIVER_VERBOSE
  "Enable verbose dev_dbg / pr_debug logging in lifted upstream chip drivers (heavy)"
  OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Static archives get folded into a shared lib downstream, so every
# component must be PIC-clean.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- libusb-1.0 ------------------------------------------------------- *

find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET "libusb-1.0>=1.0.20")

# ---- Lifted-source presence check ------------------------------------- *
#
# scripts/fetch-lifted.sh pulls upstream chip-driver sources at the
# pinned LINUX_TAG into chips/lifted/. Fail configure early with a
# friendly message if the user hasn't run the fetch step yet.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/chips/lifted/si2168/si2168.c")
  message(FATAL_ERROR
    "Lifted upstream chip drivers not present (chips/lifted/ is empty).\n"
    "Run scripts/fetch-lifted.sh before configuring CMake. See README.")
endif()

# ---- Compile-flag INTERFACE targets ----------------------------------- *
#
# INTERFACE libraries carry compile flags via target_link_libraries
# without contributing objects.
#
#   dvb_libusb_project_flags  — strict warnings for OUR code
#   dvb_libusb_chip_lift_flags — relaxed warnings + force-include of the
#                                kernel-API polyfill umbrella header for
#                                the lifted upstream chip drivers
include(CheckCCompilerFlag)

add_library(dvb_libusb_project_flags INTERFACE)
foreach(flag
    -Wall -Wextra
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wshadow
    -Wpointer-arith
    -Wno-unused-parameter)
  string(MAKE_C_IDENTIFIER "have_c_flag${flag}" var)
  check_c_compiler_flag("${flag}" ${var})
  if(${var})
    target_compile_options(dvb_libusb_project_flags INTERFACE ${flag})
  endif()
endforeach()

add_library(dvb_libusb_chip_lift_flags INTERFACE)
target_compile_options(dvb_libusb_chip_lift_flags INTERFACE
  -include linuxdvbkpi/linuxdvbkpi.h
  -Wno-unused-but-set-variable
  -Wno-unused-variable
  -Wno-unused-function
  -Wno-missing-prototypes
  -Wno-strict-prototypes
  -Wno-shadow
  -Wno-pointer-arith
  -Wno-sign-compare
  -Wno-pointer-sign
  -Wno-cast-qual
  -Wno-unused-parameter
)

# ---- Component libraries (leaf-first) --------------------------------- *

add_subdirectory(dvb_handle)      # header-only types
add_subdirectory(linuxdvbkpi)     # kernel-API polyfill
add_subdirectory(usbq)            # libusb wrapper
add_subdirectory(chips)           # all 5 lifted upstream chip drivers
add_subdirectory(bridges)         # bridge protocol + DVB engine, per family

# ---- Top-level shared library ----------------------------------------- *
#
# `dvb_libusb` is the only artifact a downstream consumer links
# against. The chip drivers and per-family DVB engines are OBJECT
# libraries so all their .o files (including ones whose symbols are
# only referenced via the module_i2c_driver constructor or the engine
# board tables walked by discover_all) end up in the shared lib —
# this is CMake's analogue of meson's link_whole.
#
# Forced SHARED: the lifted chip drivers register themselves via
# `__attribute__((constructor))` (the polyfill's module_i2c_driver
# expansion). Constructor-only TUs produce no externally-visible
# symbols, so they would be invisible in a static archive's index
# (ranlib drops them) and the chip registrations would silently
# disappear. A shared lib preserves them via runtime init.
add_library(dvb_libusb SHARED
  $<TARGET_OBJECTS:chips>
  $<TARGET_OBJECTS:dvb_em28xx>
  $<TARGET_OBJECTS:dvb_dib0700>
)
add_library(dvb_libusb::dvb_libusb ALIAS dvb_libusb)

target_link_libraries(dvb_libusb
  PUBLIC
    dvb_handle
    linuxdvbkpi
  PRIVATE
    em28xx
    dib0700
    usbq
)

# The engines' public headers (`dvb_em28xx/dvb_em28xx.h`,
# `dvb_dib0700/dvb_dib0700.h`) are part of dvb_libusb's API surface
# — tools and downstream apps include them. The engines themselves
# are OBJECT libs linked privately so their .o's fold into the
# shared lib without making the OBJECT target a transitive
# dependency (which would duplicate objects in downstream link
# lines). Re-expose the include dirs at the shared-lib boundary.
target_include_directories(dvb_libusb INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bridges/em28xx/dvb/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bridges/dib0700/dvb/include>
  $<INSTALL_INTERFACE:include>
)

set_target_properties(dvb_libusb PROPERTIES
  VERSION   ${PROJECT_VERSION}
  SOVERSION 0
  OUTPUT_NAME dvb_libusb
)

# ---- Tools ------------------------------------------------------------ *

if(DVB_LIBUSB_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

# ---- Install ---------------------------------------------------------- *

if(DVB_LIBUSB_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  # Every PUBLIC-linked dep of dvb_libusb has to land in the export
  # set, otherwise find_package consumers see dangling references
  # ("dvb_libusb::linuxdvbkpi not found"). dvb_handle is INTERFACE
  # (no on-disk artifact, just headers); linuxdvbkpi is STATIC and
  # installs its archive — its objects are also folded into the
  # shared lib, but the import target needs to resolve to a real
  # file for CMake's bookkeeping to be coherent.
  install(TARGETS dvb_libusb dvb_handle linuxdvbkpi
    EXPORT dvb_libusbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(DIRECTORY dvb_handle/include/             DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY linuxdvbkpi/include/            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY bridges/em28xx/dvb/include/     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(DIRECTORY bridges/dib0700/dvb/include/    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  # Generate dvb_libusbConfig.cmake (resolves transitive deps like
  # libusb-1.0 via pkg-config) and dvb_libusbConfigVersion.cmake
  # (handles VERSION matching for find_package). Together with
  # dvb_libusbTargets.cmake from the EXPORT set, these are what
  # makes `find_package(dvb_libusb CONFIG REQUIRED)` work.
  set(DVB_LIBUSB_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/dvb_libusb)

  configure_package_config_file(
    cmake/dvb_libusbConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dvb_libusbConfig.cmake
    INSTALL_DESTINATION ${DVB_LIBUSB_CMAKE_INSTALL_DIR}
  )
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dvb_libusbConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(EXPORT dvb_libusbTargets
    FILE dvb_libusbTargets.cmake
    NAMESPACE dvb_libusb::
    DESTINATION ${DVB_LIBUSB_CMAKE_INSTALL_DIR}
  )
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dvb_libusbConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dvb_libusbConfigVersion.cmake
    DESTINATION ${DVB_LIBUSB_CMAKE_INSTALL_DIR}
  )
endif()

# ---- Configure summary ------------------------------------------------ *

message(STATUS "dvb_libusb ${PROJECT_VERSION}")
message(STATUS "  libusb-1.0:           ${libusb_VERSION}")
message(STATUS "  C compiler:           ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  chip_driver_verbose:  ${DVB_LIBUSB_CHIP_DRIVER_VERBOSE}")
message(STATUS "  build tools:          ${DVB_LIBUSB_BUILD_TOOLS}")
message(STATUS "  install rules:        ${DVB_LIBUSB_INSTALL}")
