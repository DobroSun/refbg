project('debugger', ['c'], version : '0.1')

cc  = meson.get_compiler('c')
# cxx = meson.get_compiler('cpp')
cc_flags = [
  '-Wall',
  '-Wextra',
  '-Werror',
  '-Wpedantic',

  '-Wcomment',
  '-Wfloat-conversion',
  '-Wshadow',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',

  # @Note: cannot build stb_truetype.h with these.
  # '-Wdouble-promotion',
  # '-Wfloat-equal',
  # '-Wswitch-default',

  '-std=c23',
]

cc_flags += [
  '-Wno-unused-parameter',
  '-Wno-unused-variable',
  '-Wno-unused-function',
]


if get_option('buildtype') == 'debug'
  cc_flags += [ '-ggdb' ]
endif

if get_option('buildtype') == 'release'
  cc_flags += [ '-O2' ]
endif

add_project_arguments(cc.get_supported_arguments(cc_flags), language: 'c')
# add_project_arguments(cxx.get_supported_arguments(cc_flags), language: 'cpp') # @Note: Reuse same flags, but for cpp compiler.


incs = include_directories(['src'])

srcs = [
  'src/main.c',
  'src/temporary_storage.c',
  'src/memory_arena.c',
  'src/align.c',
  'src/base.c',
  'src/print.c',
]

protocol_base_dir = meson.current_source_dir() / 'src/wayland/protocols/'
run_command('generate-wayland-sources.sh', check: true)


srcs += [
  protocol_base_dir / 'xdg-shell.c',
  protocol_base_dir / 'cursor-shape.c',
  protocol_base_dir / 'tablet.c',
  protocol_base_dir / 'xdg-decoration.c',
]

deps = [
  dependency('wayland-client'),

  cc.find_library('m', required: true),
]

libdecor = dependency('libdecor-0', required: false)
if libdecor.found()
  # add_project_arguments('-DHAS_LIBDECOR', language: 'c')

  srcs += 'src/wayland/libdecor.c'
  deps += libdecor
endif

exe = executable(meson.project_name(), srcs, dependencies: deps, include_directories: incs)
  # install: true, install_dir: meson.project_source_root() / 'run_tree')

