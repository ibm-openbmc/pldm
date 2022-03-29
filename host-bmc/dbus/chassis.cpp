#include "chassis.hpp"

namespace pldm
{
namespace dbus
{

std::string ItemChassis::type() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Chassis::
        type();
}

std::string ItemChassis::type(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Chassis::
        type(value);
}

} // namespace dbus
} // namespace pldm
