#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Fan/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemFan = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Fan>;

class Fan : public ItemFan
{
  public:
    Fan() = delete;
    ~Fan() = default;
    Fan(const Fan&) = delete;
    Fan& operator=(const Fan&) = delete;
    Fan(Fan&&) = default;
    Fan& operator=(Fan&&) = default;

    Fan(sdbusplus::bus::bus& bus, const std::string& objPath) :
        ItemFan(bus, objPath.c_str()), path(objPath)
    {
        pldm::serialize::Serialize::getSerialize().serialize(path, "Fan");
    }

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
