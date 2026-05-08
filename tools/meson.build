# Standalone tools using the engine APIs directly. No minisatip
# dependency — these are diagnostic / scripting helpers.

executable(
  'dvb_devices',
  sources: ['dvb_devices.c'],
  dependencies: [
    dvb_handle_dep,
    engine_em28xx_dep,
    engine_dib0700_dep,
    linuxdvbkpi_dep,
  ],
)

executable(
  'dvb_pid_dump',
  sources: ['dvb_pid_dump.c'],
  dependencies: [
    dvb_handle_dep,
    engine_em28xx_dep,
    engine_dib0700_dep,
    linuxdvbkpi_dep,
  ],
)
