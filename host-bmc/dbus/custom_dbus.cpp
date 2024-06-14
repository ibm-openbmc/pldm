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

void CustomDBus::implementPCIeSlotInterface(const std::string& path)
{
    if (!pcieSlot.contains(path))
    {
        pcieSlot.emplace(path, std::make_unique<PCIeSlot>(
                                   pldm::utils::DBusHandler::getBus(), path));
    }
}

void CustomDBus::setSlotType(const std::string& path,
                             const std::string& slotType)
{
    auto typeOfSlot =
        pldm::dbus::PCIeSlot::convertSlotTypesFromString(slotType);
    if (pcieSlot.contains(path))
    {
        pcieSlot.at(path)->slotType(typeOfSlot);
    }
}

void CustomDBus::implementPCIeDeviceInterface(const std::string& path)
{
    if (!pcieDevice.contains(path))
    {
        pcieDevice.emplace(path, std::make_unique<PCIeDevice>(
                                     pldm::utils::DBusHandler::getBus(), path));
    }
}

void CustomDBus::setPCIeDeviceProps(const std::string& path, size_t lanesInUse,
                                    const std::string& value)
{
    Generations generationsInUse =
        pldm::dbus::PCIeSlot::convertGenerationsFromString(value);

    if (pcieDevice.contains(path))
    {
        pcieDevice.at(path)->lanesInUse(lanesInUse);
        pcieDevice.at(path)->generationInUse(generationsInUse);
    }
}

void CustomDBus::implementCableInterface(const std::string& path)
{
    if (!cable.contains(path))
    {
        cable.emplace(path, std::make_unique<Cable>(
                                pldm::utils::DBusHandler::getBus(), path));
    }
}

void CustomDBus::setCableAttributes(const std::string& path, double length,
                                    const std::string& cableDescription)
{
    if (cable.contains(path))
    {
        cable.at(path)->length(length);
        cable.at(path)->cableTypeDescription(cableDescription);
    }
}

void CustomDBus::implementMotherboardInterface(const std::string& path)
{
    if (!motherboard.contains(path))
    {
        motherboard.emplace(path,
                            std::make_unique<Motherboard>(
                                pldm::utils::DBusHandler::getBus(), path));
    }
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

} // namespace dbus
} // namespace pldm
