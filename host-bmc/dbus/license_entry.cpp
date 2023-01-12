#include "license_entry.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
std::string LicenseEntry::name() const
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::name();
}

std::string LicenseEntry::name(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "LicenseEntry",
                                                         "name", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::name(
        value);
}

std::string LicenseEntry::serialNumber() const
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        serialNumber();
}

std::string LicenseEntry::serialNumber(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "LicenseEntry",
                                                         "serialNumber", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        serialNumber(value);
}

auto LicenseEntry::type() const -> Type
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::type();
}

auto LicenseEntry::type(Type value) -> Type
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "LicenseEntry",
                                                         "type", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::type(
        value);
}

auto LicenseEntry::authorizationType() const -> AuthorizationType
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        authorizationType();
}

auto LicenseEntry::authorizationType(AuthorizationType value)
    -> AuthorizationType
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "LicenseEntry", "authorizationType", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        authorizationType(value);
}

uint64_t LicenseEntry::expirationTime() const
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        expirationTime();
}

uint64_t LicenseEntry::expirationTime(uint64_t value)
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "LicenseEntry", "expirationTime", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        expirationTime(value);
}

uint32_t LicenseEntry::authDeviceNumber() const
{
    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        authDeviceNumber();
}

uint32_t LicenseEntry::authDeviceNumber(uint32_t value)
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "LicenseEntry", "authDeviceNumber", value);

    return sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        authDeviceNumber(value);
}

} // namespace dbus
} // namespace pldm
