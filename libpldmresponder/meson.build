libpldmresponder_deps = [
  phosphor_dbus_interfaces,
  phosphor_logging_dep,
  nlohmann_json_dep,
  sdbusplus,
  sdeventplus,
  libpldm_dep,
  libpldmutils,
  cereal_dep
]

sources = [
  'base.cpp',
  'bios.cpp',
  'bios_table.cpp',
  'bios_attribute.cpp',
  'bios_string_attribute.cpp',
  'bios_integer_attribute.cpp',
  'bios_enum_attribute.cpp',
  'bios_config.cpp',
  'pdr_utils.cpp',
  'pdr.cpp',
  'platform.cpp',
  'platform_config.cpp',
  'fru_parser.cpp',
  'fru.cpp',
  '../host-bmc/host_pdr_handler.cpp',
  '../host-bmc/utils.cpp',
  '../host-bmc/dbus_to_event_handler.cpp',
  '../host-bmc/dbus_to_host_effecters.cpp',
  '../host-bmc/host_associations_parser.cpp',
  '../host-bmc/host_condition.cpp',
  '../host-bmc/utils.cpp',
  '../host-bmc/dbus/custom_dbus.cpp',
  '../host-bmc/dbus/serialize.cpp',
  '../host-bmc/dbus/deserialize.cpp',
  '../host-bmc/dbus/associations.cpp',
  '../host-bmc/dbus/availability.cpp',
  '../host-bmc/dbus/chassis.cpp',
  '../host-bmc/dbus/cpu_core.cpp',
  '../host-bmc/dbus/decorator_revision.cpp',
  '../host-bmc/dbus/chapdata.cpp',
  '../host-bmc/dbus/enable.cpp',
  '../host-bmc/dbus/inventory_item.cpp',
  '../host-bmc/dbus/led_group.cpp',
  '../host-bmc/dbus/license_entry.cpp',
  '../host-bmc/dbus/operational_status.cpp',
  '../host-bmc/dbus/pcie_slot.cpp',
  '../host-bmc/dbus/pcie_device.cpp',
  '../host-bmc/dbus/cable.cpp',
  '../host-bmc/dbus/asset.cpp',
  '../host-bmc/dbus/serialize.cpp',
  '../host-bmc/dbus/location_code.cpp',
  '../host-bmc/dbus/deserialize.cpp',
  '../host-bmc/dbus/pcie_topology.cpp',
  'event_parser.cpp'
]

responder_headers = ['.']

if get_option('oem-ibm').allowed()
   responder_headers += [
    '../oem/ibm/',
   ]
  sources += [
    '../oem/ibm/libpldmresponder/utils.cpp',
    '../oem/ibm/libpldmresponder/file_io.cpp',
    '../oem/ibm/libpldmresponder/file_table.cpp',
    '../oem/ibm/libpldmresponder/file_io_by_type.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_pel.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_dump.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_chap.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_cert.cpp',
    '../oem/ibm/libpldmresponder/platform_oem_ibm.cpp',
    '../oem/ibm/libpldmresponder/fru_oem_ibm.cpp',
    '../oem/ibm/libpldmresponder/oem_ibm_handler.cpp',
    '../oem/ibm/libpldmresponder/inband_code_update.cpp',
    '../oem/ibm/libpldmresponder/collect_slot_vpd.cpp',
    '../oem/ibm/requester/dbus_to_file_handler.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_progress_src.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_vpd.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_pcie.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_lic.cpp',
    '../oem/ibm/pfw-sms-utils/pfw_sms_menu.cpp',
    '../oem/ibm/libpldmresponder/file_io_type_smsmenu.cpp',
  ]
  libpam = cpp.find_library('pam', required: true)
  libpldmresponder_deps += [libpam]
endif

libpldmresponder = library(
  'pldmresponder',
  sources,
  version: meson.project_version(),
  include_directories:include_directories(responder_headers),
  dependencies: libpldmresponder_deps,
  install: true)

libpldmresponder_dep = declare_dependency(
  include_directories:include_directories(responder_headers),
  link_with: libpldmresponder)

if get_option('tests').allowed()
  subdir('test')
endif

