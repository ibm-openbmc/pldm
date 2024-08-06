#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using LocationIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Decorator::server::LocationCode>;

class LocationCode : public LocationIntf
{
  public:
    LocationCode() = delete;
    ~LocationCode() = default;
    LocationCode(const LocationCode&) = delete;
    LocationCode& operator=(const LocationCode&) = delete;
    LocationCode(LocationCode&&) = delete;
    LocationCode& operator=(LocationCode&&) = delete;

    LocationCode(sdbusplus::bus_t& bus, const std::string& objPath) :
        LocationIntf(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of LocationCode */
    std::string locationCode() const override;

    /** Set value of LocationCode */
    std::string locationCode(std::string value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
