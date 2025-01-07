#pragma once

#include <com/ibm/License/Entry/LicenseEntry/server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using LicIntf = sdbusplus::server::object_t<
    sdbusplus::com::ibm::License::Entry::server::LicenseEntry>;

class LicenseEntry : public LicIntf
{
  public:
    LicenseEntry() = delete;
    ~LicenseEntry() = default;
    LicenseEntry(const LicenseEntry&) = delete;
    LicenseEntry& operator=(const LicenseEntry&) = delete;
    LicenseEntry(LicenseEntry&&) = delete;
    LicenseEntry& operator=(LicenseEntry&&) = delete;

    LicenseEntry(sdbusplus::bus_t& bus, const std::string& objPath) :
        LicIntf(bus, objPath.c_str()), path(objPath)
    {}

    /** Get value of Name */
    std::string name() const override;

    /** Set value of Name */
    std::string name(std::string value) override;

    /** Get value of SerialNumber */
    std::string serialNumber() const override;

    /** Set value of SerialNumber */
    std::string serialNumber(std::string value) override;

    /** Get value of Type */
    Type type() const override;

    /** Set value of Type */
    Type type(Type value) override;

    /** Get value of AuthorizationType */
    AuthorizationType authorizationType() const override;

    /** Set value of AuthorizationType */
    AuthorizationType authorizationType(AuthorizationType value) override;

    /** Get value of ExpirationTime */
    uint64_t expirationTime() const override;

    /** Set value of ExpirationTime */
    uint64_t expirationTime(uint64_t value) override;

    /** Get value of AuthDeviceNumber */
    uint32_t authDeviceNumber() const override;

    /** Set value of AuthDeviceNumber */
    uint32_t authDeviceNumber(uint32_t value) override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
