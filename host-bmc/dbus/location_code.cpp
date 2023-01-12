#include "location_code.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
std::string LocationCode::locationCode() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        LocationCode::locationCode();
}

std::string LocationCode::locationCode(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "LocationCode",
                                                         "locationCode", value);

    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::
        LocationCode::locationCode(value);
}

} // namespace dbus
} // namespace pldm
