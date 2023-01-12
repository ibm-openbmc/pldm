#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Object/Enable/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using EnableIface = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Object::server::Enable>;

class Enable : public EnableIface
{
  public:
    Enable() = delete;
    ~Enable() = default;
    Enable(const Enable&) = delete;
    Enable& operator=(const Enable&) = delete;
    Enable(Enable&&) = default;
    Enable& operator=(Enable&&) = default;

    Enable(sdbusplus::bus::bus& bus, const std::string& objPath) :
        EnableIface(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of Enabled */
    bool enabled() const override;

    /** Set value of Enabled */
    bool enabled(bool value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
