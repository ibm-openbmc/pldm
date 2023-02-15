#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/PCIeDevice/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemDevice = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::PCIeDevice>;
using Generations = sdbusplus::xyz::openbmc_project::Inventory::Item::server::
    PCIeSlot::Generations;

class PCIeDevice : public ItemDevice
{
  public:
    PCIeDevice() = delete;
    ~PCIeDevice() = default;
    PCIeDevice(const PCIeDevice&) = delete;
    PCIeDevice& operator=(const PCIeDevice&) = delete;
    PCIeDevice(PCIeDevice&&) = default;
    PCIeDevice& operator=(PCIeDevice&&) = default;

    PCIeDevice(sdbusplus::bus::bus& bus, const std::string& objPath) :
        ItemDevice(bus, objPath.c_str()), path(objPath)
    {
        pldm::serialize::Serialize::getSerialize().serialize(path,
                                                             "PCIeDevice");
    }

    /** Get lanes in use */
    int64_t lanesInUse() const override;

    /** Set lanes in use */
    int64_t lanesInUse(int64_t value) override;

    /** Get Generation in use */
    Generations generationInUse() const override;

    /** Set Generation in use */
    Generations generationInUse(Generations value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
