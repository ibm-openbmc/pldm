#include "cable.hpp"

namespace pldm
{
namespace dbus
{
auto Cable::cableStatus() const -> Status
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        cableStatus();
}

auto Cable::cableStatus(Status value) -> Status
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        cableStatus(value);
}

double Cable::length() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        length();
}

double Cable::length(double value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        length(value);
}

std::string Cable::cableTypeDescription() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        cableTypeDescription();
}

std::string Cable::cableTypeDescription(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable::
        cableTypeDescription(value);
}

} // namespace dbus

} // namespace pldm
