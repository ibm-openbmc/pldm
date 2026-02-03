#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/FabricAdapter/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemFabricAdapter =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::FabricAdapter;

class FabricAdapter : public ItemFabricAdapter
{
  public:
    FabricAdapter() = delete;
    FabricAdapter(const FabricAdapter&) = delete;
    FabricAdapter& operator=(const FabricAdapter&) = delete;
    FabricAdapter(FabricAdapter&&) = delete;
    FabricAdapter& operator=(FabricAdapter&&) = delete;

    FabricAdapter(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemFabricAdapter(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath,
                                                             "FabricAdapter");
        emit_added();
    }
    ~FabricAdapter()
    {
        emit_removed();
    }
};

} // namespace dbus
} // namespace pldm
