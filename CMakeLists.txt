zephyr_library()
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library_sources(src/behavior_mouse_jiggle.c)

if(CONFIG_ZMK_POINTING)
  zephyr_library_sources(src/mouse_idle.c)
endif()
