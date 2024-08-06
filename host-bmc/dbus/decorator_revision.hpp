#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Revision/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using DecoratorRevisionIntf = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Revision>;

class DecoratorRevision : public DecoratorRevisionIntf
{
  public:
    DecoratorRevision() = delete;
    ~DecoratorRevision() = default;
    DecoratorRevision(const DecoratorRevision&) = delete;
    DecoratorRevision& operator=(const DecoratorRevision&) = delete;
    DecoratorRevision(DecoratorRevision&&) = default;
    DecoratorRevision& operator=(DecoratorRevision&&) = default;

    DecoratorRevision(sdbusplus::bus_t& bus, const std::string& objPath) :
        DecoratorRevisionIntf(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of Version */
    std::string version() const override;

    /** Set value of Version */
    std::string version(std::string value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
