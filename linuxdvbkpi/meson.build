# linuxdvbkpi — Linux kernel-API polyfill for userland chip-driver
# lift. Lets us compile drivers/media/dvb-frontends/*.c and
# drivers/media/tuners/*.c verbatim from upstream Linux without any
# source modifications.
#
# License: GPL-2.0-or-later (combined work that links the lifted
# upstream chip drivers).

linuxdvbkpi_inc = include_directories('include')

linuxdvbkpi_sources = files(
  'src/i2c.c',
  'src/i2c_mux.c',
  'src/regmap.c',
  'src/firmware.c',
  'src/intlog.c',
)

linuxdvbkpi_lib = static_library(
  'linuxdvbkpi',
  sources: linuxdvbkpi_sources,
  include_directories: linuxdvbkpi_inc,
)

# Compile-time verbosity flag for lifted chip drivers' dev_dbg /
# pr_debug output. Off by default; enable with
#   meson configure -Dchip_driver_verbose=true
# and rebuild. Heavy: hundreds of lines per tune.
linuxdvbkpi_chip_verbose_args = []
if get_option('chip_driver_verbose')
  linuxdvbkpi_chip_verbose_args += ['-DLINUXDVBKPI_VERBOSE=1']
endif

linuxdvbkpi_dep = declare_dependency(
  link_with: linuxdvbkpi_lib,
  include_directories: linuxdvbkpi_inc,
  compile_args: linuxdvbkpi_chip_verbose_args,
)
