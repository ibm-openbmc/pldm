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
                         std::make_unique<LocationCode>(
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

void CustomDBus::setSoftwareVersion(const std::string& path, std::string value)
{
    if (!softWareVersion.contains(path))
    {
        softWareVersion.emplace(
            path, std::make_unique<DecoratorRevision>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
    softWareVersion.at(path)->version(value);
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

void CustomDBus::implementCableInterface(const std::string& path)
{
    if (!cable.contains(path))
    {
        cable.emplace(path, std::make_unique<Cable>(
                                pldm::utils::DBusHandler::getBus(), path));
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

void CustomDBus::setSlotProperties(const std::string& path,
                                   const uint32_t& /*value*/,
                                   const std::string& /*linkState*/)
{
    // auto linkStatus =
    // pldm::dbus::PCIeSlot::convertStatusFromString(linkState);
    if (pcieSlot.contains(path))
    {
        // pcieSlot.at(path)->busId(value);
        //  pcieSlot.at(path)->linkStatus(linkStatus);
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

void CustomDBus::setCableAttributes(const std::string& path, double length,
                                    const std::string& cableDescription,
                                    const std::string& /*status*/)
{
    if (cable.contains(path))
    {
        cable.at(path)->length(length);
        cable.at(path)->cableTypeDescription(cableDescription);
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

void CustomDBus::implementPortInterface(const std::string& path)
{
    if (port.find(path) == port.end())
    {
        port.emplace(path,
                     std::make_unique<Port>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
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

void CustomDBus::implementLicInterfaces(
    const std::string& path, const uint32_t& authdevno, const std::string& name,
    const std::string& serialno, const uint64_t& exptime,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type& type,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        AuthorizationType& authtype)
{
    if (!codLic.contains(path))
    {
        codLic.emplace(path,
                       std::make_unique<LicenseEntry>(
                           pldm::utils::DBusHandler::getBus(), path.c_str()));
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
            path, std::make_unique<Availability>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    availabilityState.at(path)->available(state);
}

void CustomDBus::setAsserted(
    const std::string& path, const pldm_entity& entity, bool value,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser,
    uint8_t mctpEid, bool isTriggerStateEffecterStates)
{
    if (!ledGroup.contains(path))
    {
        ledGroup.emplace(
            path, std::make_unique<LEDGroup>(pldm::utils::DBusHandler::getBus(),
                                             path.c_str(), hostEffecterParser,
                                             entity, mctpEid));
    }

    ledGroup.at(path)->setStateEffecterStatesFlag(isTriggerStateEffecterStates);
    ledGroup.at(path)->asserted(value);
}

bool CustomDBus::getAsserted(const std::string& path) const
{
    if (ledGroup.find(path) != ledGroup.end())
    {
        return ledGroup.at(path)->asserted();
    }

    return false;
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
    if (!motherboard.contains(path))
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

void CustomDBus::implementFabricAdapter(const std::string& path)
{
    if (!fabricAdapter.contains(path))
    {
        fabricAdapter.emplace(
            path, std::make_unique<FabricAdapter>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }
}

void CustomDBus::implementBoard(const std::string& path)
{
    if (!board.contains(path))
    {
        board.emplace(
            path, std::make_unique<Board>(pldm::utils::DBusHandler::getBus(),
                                          path.c_str()));
    }
}

void CustomDBus::implementGlobalInterface(const std::string& path)
{
    if (!global.contains(path))
    {
        global.emplace(
            path, std::make_unique<Global>(pldm::utils::DBusHandler::getBus(),
                                           path.c_str()));
    }
}

void CustomDBus::implementPcieTopologyInterface(
    const std::string& path, uint8_t mctpEid,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser)
{
    if (pcietopology.find(path) == pcietopology.end())
    {
        pcietopology.emplace(path,
                             std::make_unique<PCIETopology>(
                                 pldm::utils::DBusHandler::getBus(),
                                 path.c_str(), hostEffecterParser, mctpEid));
    }
}

void CustomDBus::deleteObject(const std::string& path)
{
    if (location.contains(path))
    {
        location.erase(location.find(path));
    }

    if (operationalStatus.contains(path))
    {
        operationalStatus.erase(operationalStatus.find(path));
    }

    if (presentStatus.contains(path))
    {
        presentStatus.erase(presentStatus.find(path));
    }

    if (chassis.contains(path))
    {
        chassis.erase(chassis.find(path));
    }

    if (cpuCore.contains(path))
    {
        cpuCore.erase(cpuCore.find(path));
    }

    if (fan.contains(path))
    {
        fan.erase(fan.find(path));
    }

    if (connector.contains(path))
    {
        connector.erase(connector.find(path));
    }

    if (port.contains(path))
    {
        port.erase(port.find(path));
    }

    if (vrm.contains(path))
    {
        vrm.erase(vrm.find(path));
    }

    if (global.contains(path))
    {
        global.erase(global.find(path));
    }

    if (powersupply.contains(path))
    {
        powersupply.erase(powersupply.find(path));
    }

    if (board.contains(path))
    {
        board.erase(board.find(path));
    }

    if (fabricAdapter.contains(path))
    {
        fabricAdapter.erase(fabricAdapter.find(path));
    }

    if (motherboard.contains(path))
    {
        motherboard.erase(motherboard.find(path));
    }

    if (availabilityState.contains(path))
    {
        availabilityState.erase(availabilityState.find(path));
    }

    if (enabledStatus.contains(path))
    {
        enabledStatus.erase(enabledStatus.find(path));
    }

    if (pcieSlot.contains(path))
    {
        pcieSlot.erase(pcieSlot.find(path));
    }

    if (codLic.contains(path))
    {
        codLic.erase(codLic.find(path));
    }

    if (associations.contains(path))
    {
        associations.erase(associations.find(path));
    }

    if (ledGroup.contains(path))
    {
        ledGroup.erase(ledGroup.find(path));
    }

    if (cable.contains(path))
    {
        cable.erase(cable.find(path));
    }

    if (softWareVersion.contains(path))
    {
        softWareVersion.erase(softWareVersion.find(path));
    }

    if (asset.contains(path))
    {
        asset.erase(asset.find(path));
    }

    if (pcieDevice.contains(path))
    {
        pcieDevice.erase(pcieDevice.find(path));
    }
}

void CustomDBus::removeDBus(const std::vector<uint16_t> types)
{
    if (types.empty())
    {
        return;
    }

    auto savedObjs = pldm::serialize::Serialize::getSerialize().getSavedObjs();
    for (const auto& type : types)
    {
        if (!savedObjs.contains(type))
        {
            continue;
        }

        error("Deleting the dbus objects of type : {DBUS_OBJ_TYP}",
              "DBUS_OBJ_TYP", (unsigned)type);
        for (const auto& [path, entites] : savedObjs.at(type))
        {
            deleteObject(path);
        }
    }
}

void CustomDBus::updateTopologyProperty(bool value)
{
    if (pcietopology.contains("/xyz/openbmc_project/pldm"))
    {
        pcietopology.at("/xyz/openbmc_project/pldm")
            ->pcIeTopologyRefresh(value);
    }
}

} // namespace dbus
} // namespace pldm
