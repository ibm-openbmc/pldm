libpam = cpp.find_library('pam', required: true)
try_pfw_sms_deps = [libpam]

executable(
  'try-pfw-sms-menu',
  'try_pfw_sms_ibm.cpp',
  'pfw_sms_menu.cpp',
  implicit_include_directories: false,
  dependencies: try_pfw_sms_deps,
  install: true,
  install_dir: get_option('bindir'))

subdir('pam-config')

