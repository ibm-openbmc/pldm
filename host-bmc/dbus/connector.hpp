#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Connector/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemConnector = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Connector>;

class Connector : public ItemConnector
{
  public:
    Connector() = delete;
    ~Connector() = default;
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = default;
    Connector& operator=(Connector&&) = default;

    Connector(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemConnector(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath, "Connector");
    }

};

} // namespace dbus
} // namespace pldm
