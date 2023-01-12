#include "inventory_item.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
std::string InventoryItem::prettyName() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::server::Item::
        prettyName();
}

std::string InventoryItem::prettyName(std::string value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::server::Item::prettyName(
        value);
}

bool InventoryItem::present() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::server::Item::present();
}

bool InventoryItem::present(bool value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "InventoryItem",
                                                         "present", value);

    return sdbusplus::xyz::openbmc_project::Inventory::server::Item::present(
        value);
}

} // namespace dbus
} // namespace pldm
