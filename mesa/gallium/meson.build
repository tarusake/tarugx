# SPDX-License-Identifier: MIT
libtarugx = static_library(
  'tarugx',
  files('tarugx_screen.c', 'tarugx_winsys.c'),
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux,
    inc_gallium_drivers, inc_gallium_winsys,
  ],
  dependencies : [dep_libdrm],
  link_with : [libswkmsdri],
)

driver_tarugx = declare_dependency(
  compile_args : '-DGALLIUM_TARUGX',
  link_with : libtarugx,
)
