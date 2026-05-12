cmake_minimum_required(VERSION 3.16)

# CMake 3.29+ — link-line dedup that drops repeated static archives
# pulled in via both PUBLIC and transitive-PRIVATE paths. Without
# this, Apple ld emits a benign "ignoring duplicate libraries"
# warning for liblinuxdvbkpi.a.
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

project(dvb_libusb
  VERSION 0.1.1
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

# Strict -std=c11 (no GNU extensions) doesn't expose POSIX features
# like clock_gettime/CLOCK_REALTIME on Linux glibc. Request the
# POSIX.1-2008 surface project-wide so usbq/etc. compile under c11.
# macOS clang doesn't gate on this macro the same way (compiles
# regardless), so this is effectively a Linux-only fix.
add_compile_definitions(_POSIX_C_SOURCE=200809L)

# Static archives get folded into a shared lib downstream, so every
# component must be PIC-clean.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---- libusb-1.0 ------------------------------------------------------- *

find_package(PkgConfig REQUIRED)
pkg_check_modules(libusb REQUIRED IMPORTED_TARGET "libusb-1.0>=1.0.20")

# Resolve libusb's Libs.private transitives — CoreFoundation/IOKit
# framework refs on Darwin, -ludev / -lpthread / -lm on Linux when
# applicable. A downstream consumer linking libusb statically (e.g.
# minisatip with -DSTATIC=ON, or any consumer that supplies its own
# libusb-1.0.a) needs these in the final link line; the imported
# target by default only carries --libs (no Libs.private). pkg-config
# already populated libusb_STATIC_LDFLAGS_OTHER from --static --libs;
# fold it into the imported target. Harmless when libusb is consumed
# dynamically because the .dylib/.so already encodes the same refs.
if(libusb_STATIC_LDFLAGS_OTHER)
  target_link_options(PkgConfig::libusb INTERFACE ${libusb_STATIC_LDFLAGS_OTHER})
endif()

# Optional: force libusb to resolve to its static archive even when
# both .a and .dylib/.so are available in the same prefix. Useful on
# macOS where Apple's ld has no -Bstatic equivalent so the linker
# defaults to picking .dylib; on Linux a consumer-side -DSTATIC=ON
# already coerces .a selection so this is mostly a no-op there. Enable
# from the consumer side with -DDVB_LIBUSB_PREFER_LIBUSB_STATIC=ON.
option(DVB_LIBUSB_PREFER_LIBUSB_STATIC
  "Replace libusb's pkg-config link reference with a direct path to libusb-1.0.a"
  OFF)
if(DVB_LIBUSB_PREFER_LIBUSB_STATIC)
  set(_dvb_libusb_old_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  find_library(DVB_LIBUSB_LIBUSB_STATIC_PATH NAMES usb-1.0
    HINTS ${libusb_LIBDIR} ${libusb_LIBRARY_DIRS}
    NO_DEFAULT_PATH)
  if(NOT DVB_LIBUSB_LIBUSB_STATIC_PATH)
    # Fall back to default paths in case the .pc didn't carry libdir.
    find_library(DVB_LIBUSB_LIBUSB_STATIC_PATH NAMES usb-1.0 REQUIRED)
  endif()
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${_dvb_libusb_old_suffixes})
  # Swap the imported target's link to the explicit .a path. Framework
  # refs added above via target_link_options stay in place.
  set_target_properties(PkgConfig::libusb PROPERTIES
    INTERFACE_LINK_LIBRARIES "${DVB_LIBUSB_LIBUSB_STATIC_PATH}")
  message(STATUS "dvb_libusb: forcing libusb static link via ${DVB_LIBUSB_LIBUSB_STATIC_PATH}")
endif()

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

# ---- Top-level library ------------------------------------------------ *
#
# `dvb_libusb` is the only artifact a downstream consumer links
# against. Type follows BUILD_SHARED_LIBS (default for top-level use
# is shared, matching the project's historical behavior; consumers
# building a fully-static binary pass -DBUILD_SHARED_LIBS=OFF).
#
# The chip drivers and per-family DVB engines are OBJECT libraries
# so all their .o files (including ones whose symbols are only
# referenced via the module_i2c_driver constructor or the engine
# board tables walked by discover_all) end up in the final lib —
# this is CMake's analogue of meson's link_whole. The OBJECT-lib
# pattern works for both shared and static output.
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED BUILD_SHARED_LIBS)
  set(BUILD_SHARED_LIBS ON)
endif()

add_library(dvb_libusb)
add_library(dvb_libusb::dvb_libusb ALIAS dvb_libusb)

# All internal OBJECT libs are folded in via $<TARGET_OBJECTS:>
# (objects only — no transitive usage requirements through this
# path). Inter-OBJECT-lib include / link relationships are handled
# inside each subdir's CMakeLists; here we just collect their .o's.
target_sources(dvb_libusb PRIVATE
  $<TARGET_OBJECTS:usbq>
  $<TARGET_OBJECTS:em28xx>
  $<TARGET_OBJECTS:dib0700>
  $<TARGET_OBJECTS:chips>
  $<TARGET_OBJECTS:dvb_em28xx>
  $<TARGET_OBJECTS:dvb_dib0700>
)

# Direct deps of the folded objects: libusb-1.0 is referenced by
# usbq's .o files, linuxdvbkpi by basically everything. Add them at
# this level so dvb_libusb's link line resolves all symbols whether
# we ship as .so or .a — and so consumers of an installed static
# dvb_libusb don't need to know to link libusb themselves.
target_link_libraries(dvb_libusb
  PUBLIC
    dvb_handle
    linuxdvbkpi
    PkgConfig::libusb
)

# ---- Static-build constructor preservation ---------------------------- *
#
# The lifted chip drivers register themselves via the polyfill's
# `module_i2c_driver(drv)` macro, which expands to a static
# __attribute__((constructor)). Constructor-only TUs export no
# globally-visible symbols, so when libdvb_libusb is a static
# archive a downstream `ld` will not pull them in lazily — the
# i2c-driver registry stays empty and chip attach fails at runtime.
#
# Fix: when built static, ask consumers' linkers to whole-archive
# libdvb_libusb itself. We attach platform-appropriate flags as
# INTERFACE link options so this happens transparently to anyone
# linking `dvb_libusb::dvb_libusb`. Shared builds need none of this
# (the .so's runtime loader runs every constructor on dlopen).
if(NOT BUILD_SHARED_LIBS)
  # Build tree consumers (our tools/, add_subdirectory consumers):
  # the target name `dvb_libusb` is in scope, so $<TARGET_FILE:>
  # resolves to the in-tree archive path. Install tree consumers
  # (find_package): the imported target is namespaced
  # (`dvb_libusb::dvb_libusb`) and $<TARGET_FILE:dvb_libusb> doesn't
  # resolve, so we hardcode the install-prefix-relative path via
  # $<INSTALL_PREFIX>. ${CMAKE_INSTALL_LIBDIR} is a literal captured
  # at our config time — that's fine, libdvb_libusb.a always lands
  # under <prefix>/<libdir>/ by our own install(TARGETS) rule.
  include(GNUInstallDirs)
  if(APPLE)
    target_link_options(dvb_libusb INTERFACE
      "$<BUILD_INTERFACE:LINKER:-force_load,$<TARGET_FILE:dvb_libusb>>"
      "$<INSTALL_INTERFACE:LINKER:-force_load,$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libdvb_libusb.a>"
    )
  else()
    target_link_options(dvb_libusb INTERFACE
      "$<BUILD_INTERFACE:LINKER:--whole-archive>"
      "$<BUILD_INTERFACE:$<TARGET_FILE:dvb_libusb>>"
      "$<BUILD_INTERFACE:LINKER:--no-whole-archive>"
      "$<INSTALL_INTERFACE:LINKER:--whole-archive>"
      "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/libdvb_libusb.a>"
      "$<INSTALL_INTERFACE:LINKER:--no-whole-archive>"
    )
  endif()
endif()

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
  # Only install linuxdvbkpi's public api/ tree (firmware_root.h); the
  # include/ tree carries the kernel-API polyfill (linux/, asm/,
  # media/), which is an internal implementation detail and must NOT
  # be on downstream consumers' include path.
  install(DIRECTORY linuxdvbkpi/api/                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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
