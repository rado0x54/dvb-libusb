# chips — all lifted upstream Linux DVB chip drivers in one library.
# Source files live under lifted/ (gitignored); populated by
# scripts/fetch-lifted.sh from torvalds/linux at the pinned tag.
#
# Five chips today:
#   si2168    DVB-T/T2/C demod        (WinTV-dualHD DVB)
#   si2157    silicon tuner           (both WinTV-dualHD variants)
#   lgdt3306a ATSC/QAM demod          (WinTV-dualHD 01595 ATSC)
#   mn88472   DVB-T/T2/C demod        (Xbox One Tuner)
#   tda18250  silicon tuner           (Xbox One Tuner)
#
# License: GPL-2.0-or-later (each upstream file's SPDX header).

# Per-component presence check (top-level CMakeLists already verifies
# si2168.c). This loop ties any missing chip to the fetch step.
foreach(f
    lifted/si2168/si2168.c
    lifted/si2157/si2157.c
    lifted/mn88472/mn88472.c
    lifted/tda18250/tda18250.c
    lifted/lgdt3306a/lgdt3306a.c)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
    message(FATAL_ERROR
      "Lifted chip driver missing: chips/${f}.\n"
      "Run scripts/fetch-lifted.sh before configuring CMake.")
  endif()
endforeach()

# OBJECT library so every .o ends up in the final shared lib —
# constructors emitted by module_i2c_driver would otherwise be
# discarded by the linker. This is the CMake equivalent of meson's
# link_whole.
add_library(chips OBJECT
  lifted/si2168/si2168.c
  lifted/si2157/si2157.c
  lifted/mn88472/mn88472.c
  lifted/tda18250/tda18250.c
  lifted/lgdt3306a/lgdt3306a.c
)

# Each chip has a private header with the same basename as the .c
# (e.g. si2168.c includes "si2168.h" from its own dir). List every
# per-chip dir as an include path so those resolve. Headers under
# lifted/ are also exposed to the engines (they #include
# "si2168/si2168.h" and friends).
target_include_directories(chips PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lifted>
)
target_include_directories(chips PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/lifted/si2168
  ${CMAKE_CURRENT_SOURCE_DIR}/lifted/si2157
  ${CMAKE_CURRENT_SOURCE_DIR}/lifted/mn88472
  ${CMAKE_CURRENT_SOURCE_DIR}/lifted/tda18250
  ${CMAKE_CURRENT_SOURCE_DIR}/lifted/lgdt3306a
)

target_link_libraries(chips PRIVATE
  linuxdvbkpi
  dvb_libusb_chip_lift_flags
)
