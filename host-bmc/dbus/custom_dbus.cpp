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

void CustomDBus::implementObjectEnableIface(const std::string& path, bool value)
{
    if (_enabledStatus.find(path) == _enabledStatus.end())
    {
        _enabledStatus.emplace(
            path, std::make_unique<Enable>(pldm::utils::DBusHandler::getBus(),
                                           path.c_str()));
        _enabledStatus.at(path)->enabled(value);
    }
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
void CustomDBus::setCableAttributes(const std::string& path, double length,
                                    const std::string& cableDescription,
                                    const std::string& status)
{
    pldm::dbus::ItemCable::Status cableStatus =
        pldm::dbus::Cable::convertStatusFromString(status);
    if (cable.contains(path))
    {
        cable.at(path)->length(length);
        cable.at(path)->cableTypeDescription(cableDescription);
        cable.at(path)->cableStatus(cableStatus);
    }
}

void CustomDBus::setPartNumber(const std::string& path,
                               const std::string& partNumber)
{
    if (asset.contains(path))
    {
        asset.at(path)->partNumber(partNumber);
    }
}

void CustomDBus::implementAssetInterface(const std::string& path)
{
    if (!asset.contains(path))
    {
        asset.emplace(
            path, std::make_unique<Asset>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

void CustomDBus::setOperationalStatus(const std::string& path, bool status,
                                      const std::string& parentChassis)
{
    if (!status && parentChassis != "")
    {
        // If we get operational status as false for any FRU, then set
        // the critical association for it.
        std::vector<std::tuple<std::string, std::string, std::string>>
            associations{{"health_rollup", "critical", parentChassis}};
        setAssociations(path, associations);
    }

    if (operationalStatus.find(path) == operationalStatus.end())
    {
        operationalStatus.emplace(
            path, std::make_unique<OperationalStatus>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    operationalStatus.at(path)->functional(status);
}
void CustomDBus::implementPowerSupplyInterface(const std::string& path)
{
    if (powersupply.find(path) == powersupply.end())
    {
        powersupply.emplace(
            path, std::make_unique<PowerSupply>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::implementFanInterface(const std::string& path)
{
    if (fan.find(path) == fan.end())
    {
        fan.emplace(path,
                    std::make_unique<Fan>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}
void CustomDBus::implementConnecterInterface(const std::string& path)
{
    if (connector.find(path) == connector.end())
    {
        connector.emplace(
            path, std::make_unique<Connector>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::implementVRMInterface(const std::string& path)
{
    if (vrm.find(path) == vrm.end())
    {
        vrm.emplace(path,
                    std::make_unique<VRM>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

bool CustomDBus::getOperationalStatus(const std::string& path) const
{
    if (operationalStatus.find(path) != operationalStatus.end())
    {
        return operationalStatus.at(path)->functional();
    }

    return false;
}

void CustomDBus::implementFabricAdapter(const std::string& path)
{
    if (fabricAdapter.find(path) == fabricAdapter.end())
    {
        fabricAdapter.emplace(
            path, std::make_unique<FabricAdapter>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::implementBoard(const std::string& path)
{
    if (board.find(path) == board.end())
    {
        board.emplace(
            path, std::make_unique<Board>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

void CustomDBus::implementPanelInterface(const std::string& path)
{
    if (panel.find(path) == panel.end())
    {
        panel.emplace(
            path, std::make_unique<Panel>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

void CustomDBus::implementGlobalInterface(const std::string& path)
{
    if (global.find(path) == global.end())
    {
        global.emplace(
            path, std::make_unique<Global>(pldm::utils::DBusHandler::getBus(),
                                           path.c_str()));
    }
}

size_t CustomDBus::getBusId(const std::string& path) const
{
    if (pcieSlot.find(path) != pcieSlot.end())
    {
        return pcieSlot.at(path)->busId();
    }
    return 0;
}

void CustomDBus::implementCableInterface(const std::string& path)
{
    if (!cable.contains(path))
    {
        cable.emplace(
            path, std::make_unique<Cable>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

void CustomDBus::updateItemPresentStatus(const std::string& path,
                                         bool isPresent)
{
    if (presentStatus.find(path) == presentStatus.end())
    {
        presentStatus.emplace(
            path, std::make_unique<InventoryItem>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
        std::filesystem::path ObjectPath(path);

        // Hardcode the present dbus property to true
        presentStatus.at(path)->present(true);

        // Set the pretty name dbus property to the filename
        // form the dbus path object
        presentStatus.at(path)->prettyName(ObjectPath.filename());
    }
    else
    {
        // object is already created
        presentStatus.at(path)->present(isPresent);
    }
}

void CustomDBus::implementChassisInterface(const std::string& path)
{
    if (chassis.find(path) == chassis.end())
    {
        chassis.emplace(path,
                        std::make_unique<ItemChassis>(
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
void CustomDBus::setAvailabilityState(const std::string& path,
                                      const bool& state)
{
    if (availabilityState.find(path) == availabilityState.end())
    {
        availabilityState.emplace(
            path, std::make_unique<Availability>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    availabilityState.at(path)->available(state);
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
