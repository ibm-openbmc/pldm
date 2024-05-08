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
        cpuCore.emplace(
            path, std::make_unique<CPUCore>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
    }
}

void CustomDBus::setMicrocode(const std::string& path, uint32_t value)
{
    if (!cpuCore.contains(path))
    {
        cpuCore.emplace(
            path, std::make_unique<CPUCore>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
    }
    cpuCore.at(path)->microcode(value);
}

void CustomDBus::implementMotherboardInterface(const std::string& path)
{
    if (motherboard.find(path) == motherboard.end())
    {
        motherboard.emplace(
            path, std::make_unique<Motherboard>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::setAssociations(const std::string& path, AssociationsObj assoc)
{
    using PropVariant = sdbusplus::xyz::openbmc_project::Association::server::
        Definitions::PropertiesVariant;

    if (associations.find(path) == associations.end())
    {
        PropVariant value{std::move(assoc)};
        std::map<std::string, PropVariant> properties;
        properties.emplace("Associations", std::move(value));

        associations.emplace(path, std::make_unique<Associations>(
                                       pldm::utils::DBusHandler::getBus(),
                                       path.c_str(), properties));
    }
    else
    {
        // object already created , so just update the associations
        auto currentAssociations = getAssociations(path);
        AssociationsObj newAssociations;

        for (const auto& association : assoc)
        {
            if (std::find(currentAssociations.begin(),
                          currentAssociations.end(),
                          association) != currentAssociations.end())
            {
                // association is present in current associations
                // do nothing
            }
            else
            {
                currentAssociations.push_back(association);
            }
        }

        associations.at(path)->associations(currentAssociations);
    }
}

const AssociationsObj CustomDBus::getAssociations(const std::string& path)
{
    if (associations.find(path) != associations.end())
    {
        return associations.at(path)->associations();
    }
    return {};
}

void CustomDBus::implementPCIeSlotInterface(const std::string& path)
{
    if (pcieSlot.find(path) == pcieSlot.end())
    {
        pcieSlot.emplace(
            path, std::make_unique<PCIeSlot>(pldm::utils::DBusHandler::getBus(),
                                             path.c_str()));
    }
}

void CustomDBus::setSlotProperties(const std::string& path,
                                   const uint32_t& value,
                                   const std::string& linkState)
{
    auto linkStatus = pldm::dbus::PCIeSlot::convertStatusFromString(linkState);
    if (pcieSlot.contains(path))
    {
        pcieSlot.at(path)->busId(value);
        pcieSlot.at(path)->linkStatus(linkStatus);
    }
}

void CustomDBus::setSlotType(const std::string& path,
                             const std::string& slotType)
{
    auto slottype = pldm::dbus::PCIeSlot::convertSlotTypesFromString(slotType);
    if (pcieSlot.contains(path))
    {
        pcieSlot.at(path)->slotType(slottype);
    }
}

void CustomDBus::implementPCIeDeviceInterface(const std::string& path)
{
    if (!pcieDevice.contains(path))
    {
        pcieDevice.emplace(
            path, std::make_unique<PCIeDevice>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::setPCIeDeviceProps(const std::string& path, int64_t lanesInuse,
                                    const std::string& value)
{
    Generations generationsInuse =
        pldm::dbus::PCIeSlot::convertGenerationsFromString(value);

    if (pcieDevice.contains(path))
    {
        pcieDevice.at(path)->lanesInUse(lanesInuse);
        pcieDevice.at(path)->generationInUse(generationsInuse);
    }
}


} // namespace dbus
} // namespace pldm
