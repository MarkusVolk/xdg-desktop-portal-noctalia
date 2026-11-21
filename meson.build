project('xdg-desktop-portal-noctalia', ['c', 'cpp'],
  version: files('VERSION'),
  meson_version: '>=1.1',
  license: 'MIT',
  default_options: [
    'cpp_std=c++23',
    'cpp_extensions=false',
    'warning_level=3',
    'b_ndebug=if-release',
  ],
)

glib_dep = dependency('glib-2.0', version: '>=2.76')
gio_dep = dependency('gio-2.0', version: '>=2.76')
gio_unix_dep = dependency('gio-unix-2.0', version: '>=2.76')

if get_option('buildtype') != 'release'
  add_project_arguments('-D_GLIBCXX_ASSERTIONS', language: 'cpp')
endif

libexecdir = get_option('prefix') / get_option('libexecdir')

core = static_library('xdpn-core',
  files(
    'src/dbus/interface.cpp',
    'src/inhibit/inhibit.cpp',
    'src/settings/settings.cpp',
  ),
  include_directories: include_directories('src'),
  dependencies: [glib_dep, gio_dep, gio_unix_dep],
)

executable('xdg-desktop-portal-noctalia',
  files('src/main.cpp'),
  include_directories: include_directories('src'),
  link_with: core,
  dependencies: [glib_dep, gio_dep, gio_unix_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)

data_conf = configuration_data()
data_conf.set('libexecdir', libexecdir)

install_data('data/noctalia.portal',
  install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals')

configure_file(
  input: 'data/org.freedesktop.impl.portal.desktop.noctalia.service.in',
  output: 'org.freedesktop.impl.portal.desktop.noctalia.service',
  configuration: data_conf,
  install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

configure_file(
  input: 'data/xdg-desktop-portal-noctalia.service.in',
  output: 'xdg-desktop-portal-noctalia.service',
  configuration: data_conf,
  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'user',
)

if get_option('tests')
  test('settings', executable('settings-test',
    files('tests/settings_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep],
  ))
  test('inhibit', executable('inhibit-test',
    files('tests/inhibit_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep],
  ))
endif
