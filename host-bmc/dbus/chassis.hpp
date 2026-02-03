#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Chassis/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemChassisIntf =
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Chassis;

class ItemChassis : public ItemChassisIntf
{
  public:
    ItemChassis() = delete;
    ItemChassis(const ItemChassis&) = delete;
    ItemChassis& operator=(const ItemChassis&) = delete;
    ItemChassis(ItemChassis&&) = delete;
    ItemChassis& operator=(ItemChassis&&) = delete;

    ItemChassis(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemChassisIntf(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath,
                                                             "ItemChassis");
        emit_added();
    }
    ~ItemChassis()
    {
        emit_removed();
    }

    /** Get value of Type */
    ChassisType type() const override;

    /** Set value of Type */
    ChassisType type(ChassisType value) override;
};

} // namespace dbus
} // namespace pldm
