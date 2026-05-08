# Aggregator for the em28xx bridge family — pulls in the bridge
# protocol lib + the DVB engine that sits on top of it. Top-level
# meson.build subdir's this directory; consumers depend on
# em28xx_family_dep to get both layers in one shot.

subdir('bridge')
subdir('dvb')

em28xx_family_dep = declare_dependency(
  dependencies: [em28xx_dep, dvb_em28xx_dep],
)
