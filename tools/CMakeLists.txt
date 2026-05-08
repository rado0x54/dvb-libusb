# Standalone tools using the bundled dvb_libusb shared library.

add_executable(dvb_devices dvb_devices.c)
target_link_libraries(dvb_devices PRIVATE dvb_libusb dvb_libusb_project_flags)

add_executable(dvb_pid_dump dvb_pid_dump.c)
target_link_libraries(dvb_pid_dump PRIVATE dvb_libusb dvb_libusb_project_flags)

if(DVB_LIBUSB_INSTALL)
  include(GNUInstallDirs)
  install(TARGETS dvb_devices dvb_pid_dump
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
