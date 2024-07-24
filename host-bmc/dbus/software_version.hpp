#pragma once

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using SoftWareVersionIntf = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Software::server::Version>;

class SoftWareVersion : public SoftWareVersionIntf
{
  public:
    SoftWareVersion() = delete;
    ~SoftWareVersion() = default;
    SoftWareVersion(const SoftWareVersion&) = delete;
    SoftWareVersion& operator=(const SoftWareVersion&) = delete;

    SoftWareVersion(sdbusplus::bus_t& bus, const std::string& objPath) :
        SoftWareVersionIntf(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of Version */
    std::string version() const override;

    /** Set value of Version */
    std::string version(std::string value) override;

    /** Get value of Purpose */
    VersionPurpose purpose() const override;

    /** Set value of Purpose */
    VersionPurpose purpose(VersionPurpose value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
