#include "custom_dbus.hpp"

#include "libpldm/state_set.h"

namespace pldm
{
namespace dbus
{

void CustomDBus::setLocationCode(const std::string& path, std::string value)
{
    if (location.find(path) == location.end())
    {
        location.emplace(path,
                         std::make_unique<LocationCode>(
                             pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    location.at(path)->locationCode(value);
}

std::string CustomDBus::getLocationCode(const std::string& path) const
{
    if (location.find(path) != location.end())
    {
        return location.at(path)->locationCode();
    }

    return {};
}

void CustomDBus::setSoftwareVersion(const std::string& path, std::string value)
{
    if (softWareVersion.find(path) == softWareVersion.end())
    {
        softWareVersion.emplace(
            path, std::make_unique<SoftWareVersion>(
                      pldm::utils::DBusHandler::getBus(), path.c_str()));
    }

    softWareVersion.at(path)->version(value);
    softWareVersion.at(path)->purpose(
        sdbusplus::xyz::openbmc_project::Software::server::Version::
            VersionPurpose::Other);
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
    if (operationalStatus.find(path) != operationalStatus.end())
    {
        return operationalStatus.at(path)->functional();
    }

    return false;
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
    if (pcieSlot.find(path) == pcieSlot.end())
    {
        pcieSlot.emplace(
            path, std::make_unique<PCIeSlot>(pldm::utils::DBusHandler::getBus(),
                                             path.c_str()));
    }
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

void CustomDBus::implementCpuCoreInterface(const std::string& path)
{
    if (cpuCore.find(path) == cpuCore.end())
    {
        cpuCore.emplace(
            path, std::make_unique<CPUCore>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
    }
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

void CustomDBus::implementGlobalInterface(const std::string& path)
{
    if (global.find(path) == global.end())
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

void CustomDBus::implementLicInterfaces(
    const std::string& path, const uint32_t& authdevno, const std::string& name,
    const std::string& serialno, const uint64_t& exptime,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type& type,
    const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
        AuthorizationType& authtype)
{
    if (codLic.find(path) == codLic.end())
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
    if (availabilityState.find(path) == availabilityState.end())
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
    if (ledGroup.find(path) == ledGroup.end())
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
        newAssociations.reserve(currentAssociations.size() + assoc.size());
        newAssociations.insert(newAssociations.end(),
                               currentAssociations.begin(),
                               currentAssociations.end());
        newAssociations.insert(newAssociations.end(), assoc.begin(),
                               assoc.end());
        associations.at(path)->associations(newAssociations);
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

void CustomDBus::setMicrocode(const std::string& path, uint32_t value)
{
    if (cpuCore.find(path) == cpuCore.end())
    {
        cpuCore.emplace(
            path, std::make_unique<CPUCore>(pldm::utils::DBusHandler::getBus(),
                                            path.c_str()));
    }
    cpuCore.at(path)->microcode(value);
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
