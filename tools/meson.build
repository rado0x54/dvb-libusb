# Standalone tools using the bundled libdvb_usb shared library.

executable(
  'dvb_devices',
  sources: ['dvb_devices.c'],
  dependencies: [dvb_usb_dep],
)

executable(
  'dvb_pid_dump',
  sources: ['dvb_pid_dump.c'],
  dependencies: [dvb_usb_dep],
)
