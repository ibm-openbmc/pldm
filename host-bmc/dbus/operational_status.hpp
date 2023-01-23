#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using OperationalStatusIntf =
    sdbusplus::server::object::object<sdbusplus::xyz::openbmc_project::State::
                                          Decorator::server::OperationalStatus>;

class OperationalStatus : public OperationalStatusIntf
{
  public:
    OperationalStatus() = delete;
    ~OperationalStatus() = default;
    OperationalStatus(const OperationalStatus&) = delete;
    OperationalStatus& operator=(const OperationalStatus&) = delete;
    OperationalStatus(OperationalStatus&&) = default;
    OperationalStatus& operator=(OperationalStatus&&) = default;

    OperationalStatus(sdbusplus::bus::bus& bus, const std::string& objPath) :
        OperationalStatusIntf(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of Functional */
    bool functional() const override;

    /** Set value of Functional */
    bool functional(bool value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
