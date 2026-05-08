# usbq — libusb wrapper providing the USB-IO surface every chip-driver
# layer above it consumes (em28xx, dib0700, the engines, the tools).
#
# License: GPL-2.0-or-later (combined work).

usbq_inc = include_directories('include')

usbq_lib = static_library(
  'usbq',
  sources: files('src/usbq.c'),
  include_directories: usbq_inc,
  dependencies: [libusb_dep],
)

usbq_dep = declare_dependency(
  link_with: usbq_lib,
  include_directories: usbq_inc,
  dependencies: [libusb_dep],
)
