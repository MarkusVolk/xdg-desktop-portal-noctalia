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
nlohmann_json_dep = dependency('nlohmann_json')
cups_dep = dependency('cups', required: get_option('print'))

if get_option('dialog')
  # Recent GLib headers use __COUNTER__, a C2y extension clang warns about under lib/'s -Wpedantic. lib/ adds
  # GLib with -I after project arguments, so mark its include directory as a system directory (-isystem wins).
  # pkg-config reports the directory without the sysroot; a cross build gets it from the cross file.
  glib_includedir = glib_dep.get_variable(pkgconfig: 'includedir')
  glib_sys_root = meson.get_external_property('sys_root', '')
  if glib_sys_root != '' and not glib_includedir.startswith(glib_sys_root)
    glib_includedir = glib_sys_root + glib_includedir
  endif
  add_project_arguments('-isystem', glib_includedir / 'glib-2.0', language: 'cpp')
endif

if get_option('buildtype') != 'release'
  add_project_arguments('-D_GLIBCXX_ASSERTIONS', language: 'cpp')
endif

libexecdir = get_option('prefix') / get_option('libexecdir')
assetsdir = get_option('prefix') / get_option('datadir') / meson.project_name() / 'assets'

core_sources = []
core_dependencies = [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep]
core_args = ['-DXDPN_DIALOG_PATH="@0@"'.format(libexecdir / 'noctalia-portal-dialog')]
if get_option('print')
  core_sources += files('src/print/cups_printers.cpp', 'src/print/print.cpp', 'src/print/print_settings.cpp')
  core_dependencies += cups_dep
  core_args += '-DXDPN_HAVE_PRINT'
endif

core = static_library('xdpn-core',
  core_sources + files(
    'src/access/access.cpp',
    'src/account/account.cpp',
    'src/appchooser/appchooser.cpp',
    'src/dbus/dialog_calls.cpp',
    'src/dbus/dialog_process.cpp',
    'src/dbus/interface.cpp',
    'src/dbus/options.cpp',
    'src/dialog/request.cpp',
    'src/email/email.cpp',
    'src/icons/icon_lookup.cpp',
    'src/inhibit/inhibit.cpp',
    'src/launcher/launcher.cpp',
    'src/lockdown/lockdown.cpp',
    'src/notification/notification.cpp',
    'src/settings/settings.cpp',
    'src/wallpaper/wallpaper.cpp',
  ),
  include_directories: include_directories('src'),
  cpp_args: core_args,
  dependencies: core_dependencies,
)

executable('xdg-desktop-portal-noctalia',
  files('src/main.cpp'),
  cpp_args: core_args,
  include_directories: include_directories('src'),
  link_with: core,
  dependencies: [glib_dep, gio_dep, gio_unix_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)

if get_option('dialog')
  noctalia_ui_build_codecs = true
  subdir('lib')

  build_config = configuration_data()
  build_config.set_quoted('XDPN_ASSETS_DIR', assetsdir)
  build_config_h = configure_file(output: 'xdpn_build_config.h', configuration: build_config)

  executable('noctalia-portal-dialog',
    files(
      'src/dialog/main.cpp',
      'src/dialog/dialog_window.cpp',
      'src/dialog/glib_poll_source.cpp',
      'src/dialog/main_loop.cpp',
      'src/dialog/palette_file.cpp',
    ) + noctalia_ui_protocol_headers + [build_config_h],
    include_directories: [include_directories('src'), include_directories('.')],
    link_with: core,
    dependencies: [noctalia_ui_dep, noctalia_ui_codecs_dep, glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep,
                   dependency('tomlplusplus', version: '>=3.4'), dependency('wayland-client')],
    install: true,
    install_dir: get_option('libexecdir'),
  )
  install_subdir(noctalia_ui_assets_dir / 'fonts', install_dir: assetsdir)
  install_data('data/templates/colors.toml',
    install_dir: get_option('datadir') / meson.project_name() / 'templates')
endif

data_conf = configuration_data()
data_conf.set('libexecdir', libexecdir)

portal_interfaces = ['Settings', 'Inhibit', 'Access', 'AppChooser', 'Lockdown', 'Email', 'Notification', 'Account',
                     'Wallpaper', 'DynamicLauncher']
if get_option('print')
  portal_interfaces += 'Print'
endif
portal_conf = configuration_data()
portal_conf.set('INTERFACES', 'org.freedesktop.impl.portal.' + ';org.freedesktop.impl.portal.'.join(portal_interfaces) + ';')
configure_file(
  input: 'data/noctalia.portal.in',
  output: 'noctalia.portal',
  configuration: portal_conf,
  install_dir: get_option('datadir') / 'xdg-desktop-portal' / 'portals',
)

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
  test('request', executable('request-test',
    files('tests/request_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep],
  ))
  test('icon-lookup', executable('icon-lookup-test',
    files('tests/icon_lookup_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep],
  ))
  test('email', executable('email-test',
    files('tests/email_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep],
  ))
  test('notification', executable('notification-test',
    files('tests/notification_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep],
  ))
  test('dialog-portals', executable('dialog-portals-test',
    files('tests/dialog_portals_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep, nlohmann_json_dep],
  ))
  if get_option('print')
    test('print-settings', executable('print-settings-test',
      files('tests/print_settings_test.cpp'),
      include_directories: include_directories('src'),
      link_with: core,
      dependencies: core_dependencies,
    ))
  endif
  test('inhibit', executable('inhibit-test',
    files('tests/inhibit_test.cpp'),
    include_directories: include_directories('src'),
    link_with: core,
    dependencies: [glib_dep, gio_dep, gio_unix_dep],
  ))
endif
