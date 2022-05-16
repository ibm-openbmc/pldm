#include "pcie_slot.hpp"

namespace pldm
{
namespace dbus
{

auto PCIeSlot::generation() const -> Generations
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        generation();
}

auto PCIeSlot::generation(Generations value) -> Generations
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        generation(value);
}

auto PCIeSlot::linkStatus(Status value) -> Status
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        linkStatus(value);
}

size_t PCIeSlot::lanes() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        lanes();
}

size_t PCIeSlot::lanes(size_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        lanes(value);
}

auto PCIeSlot::slotType() const -> SlotTypes
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        slotType();
}

auto PCIeSlot::slotType(SlotTypes value) -> SlotTypes
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        slotType(value);
}

bool PCIeSlot::hotPluggable() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        hotPluggable();
}

bool PCIeSlot::hotPluggable(bool value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        hotPluggable(value);
}
size_t PCIeSlot::busId() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        busId();
}

size_t PCIeSlot::busId(size_t value)
{
    return sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeSlot::
        busId(value);
}

} // namespace dbus
} // namespace pldm
