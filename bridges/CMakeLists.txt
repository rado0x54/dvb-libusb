# bridges — container directory. One subdir per supported USB bridge
# family. Each family bundles its own bridge-protocol lib and the
# DVB engine sitting on top.
#
# Adding a new bridge family = one new subdir like cx231xx/ with
# bridge/ + dvb/ inside, and an add_subdirectory line here. The
# top-level CMakeLists then needs to pull the new family-level
# OBJECT lib into dvb_libusb via $<TARGET_OBJECTS:dvb_cx231xx>.

add_subdirectory(em28xx)
add_subdirectory(dib0700)
add_subdirectory(dvbsky)
