# bridges — container directory. One subdir per supported USB bridge
# family. Each family bundles its own bridge-protocol lib and the
# DVB engine sitting on top.
#
# Adding a new bridge family = one new subdir like cx231xx/ with
# bridge/ + dvb/ inside. Listed below; top-level meson.build doesn't
# need to change beyond pulling in any new family-level dep that
# libdvb_usb wants to link.

subdir('em28xx')
subdir('dib0700')
