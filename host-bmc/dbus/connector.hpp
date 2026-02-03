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
using ItemConnector =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Connector;

class Connector : public ItemConnector
{
  public:
    Connector() = delete;
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;

    Connector(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemConnector(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath,
                                                             "Connector");
        emit_added();
    }
    ~Connector()
    {
        emit_removed();
    }
};

} // namespace dbus
} // namespace pldm
