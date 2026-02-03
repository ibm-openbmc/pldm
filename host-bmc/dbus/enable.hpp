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
using EnableIface = sdbusplus::xyz::openbmc_project::Object::server::Enable;

class Enable : public EnableIface
{
  public:
    Enable() = delete;
    Enable(const Enable&) = delete;
    Enable& operator=(const Enable&) = delete;

    Enable(sdbusplus::bus_t& bus, const std::string& objPath) :
        EnableIface(bus, objPath.c_str()), path(objPath)
    {
        emit_added();
    }
    ~Enable()
    {
        emit_removed();
    }

    /** Get value of Enabled */
    bool enabled() const override;

    /** Set value of Enabled */
    bool enabled(bool value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
