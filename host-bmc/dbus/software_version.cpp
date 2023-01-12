#include "software_version.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
std::string SoftWareVersion::version() const
{
    return sdbusplus::xyz::openbmc_project::Software::server::Version::
        version();
}

std::string SoftWareVersion::version(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "SoftWareVersion", "version", value);

    return sdbusplus::xyz::openbmc_project::Software::server::Version::version(
        value);
}

auto SoftWareVersion::purpose() const -> VersionPurpose
{
    return sdbusplus::xyz::openbmc_project::Software::server::Version::
        purpose();
}

auto SoftWareVersion::purpose(VersionPurpose value) -> VersionPurpose
{
    return sdbusplus::xyz::openbmc_project::Software::server::Version::purpose(
        value);
}

} // namespace dbus
} // namespace pldm
