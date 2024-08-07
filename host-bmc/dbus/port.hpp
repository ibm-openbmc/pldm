#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Connector/Port/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemPort = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Inventory::Connector::server::Port>;

class Port : public ItemPort
{
  public:
    Port() = delete;
    ~Port() = default;
    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;

    Port(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemPort(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath, "Port");
    }
};

} // namespace dbus
} // namespace pldm
