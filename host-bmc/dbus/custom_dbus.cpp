#include "custom_dbus.hpp"

namespace pldm
{
namespace dbus
{
void CustomDBus::setLocationCode(const std::string& path, std::string value)
{
    if (!location.contains(path))
    {
        location.emplace(path,
                         std::make_unique<LocationIntf>(
                             pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    location.at(path)->locationCode(value);
}

std::optional<std::string>
    CustomDBus::getLocationCode(const std::string& path) const
{
    if (location.contains(path))
    {
        return location.at(path)->locationCode();
    }

    return std::nullopt;
}

void CustomDBus::setOperationalStatus(const std::string& path, uint8_t status)
{
    if (!operationalStatus.contains(path))
    {
        operationalStatus.emplace(
            path, std::make_unique<OperationalStatusIntf>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    if (status == PLDM_STATE_SET_OPERATIONAL_STRESS_STATUS_NORMAL)
    {
        operationalStatus.at(path)->functional(true);
    }
    else
    {
        operationalStatus.at(path)->functional(false);
    }
}

bool CustomDBus::getOperationalStatus(const std::string& path) const
{
    if (operationalStatus.contains(path))
    {
        return operationalStatus.at(path)->functional();
    }

    return false;
}

void CustomDBus::implementChapDataInterface(
    const std::string& path,
    pldm::responder::oem_fileio::Handler* dbusToFilehandlerObj)
{
    if (chapdata.find(path) == chapdata.end())
    {
        chapdata.emplace(path, std::make_unique<ChapDatas>(
                                   pldm::utils::DBusHandler::getBus(),
                                   path.c_str(), dbusToFilehandlerObj));
    }
}
void CustomDBus::implementLicInterfaces(
    const std::string& path, const uint32_t& authdevno, const std::string& name,
    const std::string& serialno, const uint64_t& exptime,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type& type,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        AuthorizationType& authtype)
{
    if (!codLic.contains(path))
    {
        codLic.emplace(
            path, std::make_unique<LicIntf>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
    }

    codLic.at(path)->authDeviceNumber(authdevno);
    codLic.at(path)->name(name);
    codLic.at(path)->serialNumber(serialno);
    codLic.at(path)->expirationTime(exptime);
    codLic.at(path)->type(type);
    codLic.at(path)->authorizationType(authtype);
}

void CustomDBus::setAvailabilityState(const std::string& path,
                                      const bool& state)
{
    if (!availabilityState.contains(path))
    {
        availabilityState.emplace(
            path, std::make_unique<AvailabilityIntf>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    availabilityState.at(path)->available(state);
}

void CustomDBus::implementCpuCoreInterface(const std::string& path)
{
    if (!cpuCore.contains(path))
    {
        cpuCore.emplace(path, std::make_unique<CPUCore>(
                                  pldm::utils::DBusHandler::getBus(), path));
    }
}

void CustomDBus::setMicroCode(const std::string& path, uint32_t value)
{
    if (!cpuCore.contains(path))
    {
        cpuCore.emplace(path, std::make_unique<CPUCore>(
                                  pldm::utils::DBusHandler::getBus(), path));
    }
    cpuCore.at(path)->microcode(value);
}

std::optional<uint32_t> CustomDBus::getMicroCode(const std::string& path) const
{
    if (cpuCore.contains(path))
    {
        return cpuCore.at(path)->microcode();
    }
    return std::nullopt;
}

void CustomDBus::implementMotherboardInterface(const std::string& path)
{
    if (motherboard.find(path) == motherboard.end())
    {
        motherboard.emplace(path,
                            std::make_unique<Motherboard>(
                                pldm::utils::DBusHandler::getBus(), path));
    }
}

void CustomDBus::implementObjectEnableIface(const std::string& path, bool value)
{
    if (!enabledStatus.contains(path))
    {
        enabledStatus.emplace(
            path, std::make_unique<Enable>(pldm::utils::DBusHandler::getBus(),
                                           path.c_str()));
    }
    enabledStatus.at(path)->enabled(value);
}

} // namespace dbus
} // namespace pldm
