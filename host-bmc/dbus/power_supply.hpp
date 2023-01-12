#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/PowerSupply/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemPowerSupply = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::PowerSupply>;

class PowerSupply : public ItemPowerSupply
{
  public:
    PowerSupply() = delete;
    ~PowerSupply() = default;
    PowerSupply(const PowerSupply&) = delete;
    PowerSupply& operator=(const PowerSupply&) = delete;
    PowerSupply(PowerSupply&&) = default;
    PowerSupply& operator=(PowerSupply&&) = default;

    PowerSupply(sdbusplus::bus::bus& bus, const std::string& objPath) :
        ItemPowerSupply(bus, objPath.c_str()), path(objPath)
    {
        pldm::serialize::Serialize::getSerialize().serialize(path,
                                                             "PowerSupply");
    }

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
