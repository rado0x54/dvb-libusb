em28xx_inc = include_directories('include')

em28xx_lib = static_library(
  'em28xx',
  sources: [
    'src/em28xx_core.c',
    'src/em28xx_i2c.c',
    'src/em28xx_gpio.c',
    'src/em28xx_ts.c',
  ],
  include_directories: em28xx_inc,
  # linuxdvbkpi: needed for the struct i2c_adapter shim that
  # em28xx_get_i2c_adapter() returns to upstream chip drivers.
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)

em28xx_dep = declare_dependency(
  link_with: em28xx_lib,
  include_directories: em28xx_inc,
  dependencies: [usbq_dep, linuxdvbkpi_dep],
)
