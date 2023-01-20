#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Cable/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{

using ItemCable = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Cable>;

class Cable : public ItemCable
{
  public:
    Cable() = delete;
    ~Cable() = default;
    Cable(const Cable&) = delete;
    Cable& operator=(const Cable&) = delete;
    Cable(Cable&&) = default;
    Cable& operator=(Cable&&) = default;

    Cable(sdbusplus::bus::bus& bus, const std::string& objPath) :
        ItemCable(bus, objPath.c_str()), path(objPath)
    {
        // cable objects does not need to be store in serialized memory
    }

    /** Get value of Generation */
    double length() const override;

    /** Set value of Generation */
    double length(double value) override;

    /** Get value of Lanes */
    std::string cableTypeDescription() const override;

    /** Set value of Lanes */
    std::string cableTypeDescription(std::string value) override;

    /** Get value of SlotType */
    Status cableStatus() const override;

    /** Set value of SlotType */
    Status cableStatus(Status value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
