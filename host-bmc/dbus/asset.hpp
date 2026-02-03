#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{

using ItemAsset =
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Asset;

class Asset : public ItemAsset
{
  public:
    Asset() = delete;
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) = delete;
    Asset& operator=(Asset&&) = delete;

    Asset(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemAsset(bus, objPath.c_str())
    {
        emit_added();
    }
    ~Asset()
    {
        emit_removed();
    }

    /** Set Part Number */
    std::string partNumber(std::string value) override;
};

} // namespace dbus
} // namespace pldm
