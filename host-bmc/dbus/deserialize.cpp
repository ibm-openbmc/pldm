#include "deserialize.hpp"

#include "custom_dbus.hpp"
#include "serialize.hpp"

#include <libpldm/pdr.h>

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace deserialize
{
namespace fs = std::filesystem;
using namespace pldm::utils;

using Json = nlohmann::json;
using Properties = std::map<std::string, dbus::PropertyValue>;

using callback =
    std::function<void(const std::string& path, Properties values)>;

std::unordered_map<std::string, callback> dBusInterfaceHandler{
    {"LocationCode",
     [](const std::string& path, Properties values) {
    if (values.contains("locationCode"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().setLocationCode(
            path, std::get<std::string>(values.at("locationCode")));
    }
}},
    {"Associations",
     [](const std::string& path, Properties values) {
    if (values.contains("associations"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().setAssociations(
            path, std::get<dbus::AssociationsObj>(values.at("associations")));
    }
}},
    {"Available",
     [](const std::string& path, Properties values) {
    if (values.contains("available"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().setAvailabilityState(
            path, std::get<bool>(values.at("available")));
    }
}},
    {"OperationalStatus",
     [](const std::string& path, Properties values) {
    if (values.contains("functional"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().setOperationalStatus(
            path, std::get<bool>(values.at("functional")));
    }
}},
    {"InventoryItem",
     [](const std::string& path, Properties values) {
    if (values.contains("present"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().updateItemPresentStatus(
            path, std::get<bool>(values.at("present")));
    }
}},
    {"Enable",
     [](const std::string& path, Properties values) {
    if (values.contains("enabled"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().implementObjectEnableIface(
            path, std::get<bool>(values.at("enabled")));
    }
}},
    {"ItemChassis",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementChassisInterface(path);
}},
    {"PCIeSlot",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementPCIeSlotInterface(path);
}},
    {"CPUCore",
     [](const std::string& path, Properties values) {
    if (values.contains("microcode"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().setMicroCode(
            path, std::get<uint32_t>(values.at("microcode")));
    }
    else
    {
        pldm::dbus::CustomDBus::getCustomDBus().implementCpuCoreInterface(path);
    }
}},
    {"Enable",
     [](const std::string& path, Properties values) {
    if (values.contains("enabled"))
    {
        pldm::dbus::CustomDBus::getCustomDBus().implementObjectEnableIface(
            path, std::get<bool>(values.at("enabled")));
    }
}},
    {"Motherboard",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementMotherboardInterface(path);
}},
    {"PowerSupply",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementPowerSupplyInterface(path);
}},
    {"Fan",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementFanInterface(path);
}},
    {"Connector",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementConnecterInterface(path);
}},
    {"Port",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementPortInterface(path);
}},
    {"VRM",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementVRMInterface(path);
}},
    {"FabricAdapter",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementFabricAdapter(path);
}},
    {"Board",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementBoard(path);
}},
    {"Global",
     [](const std::string& path, Properties /* values */) {
    pldm::dbus::CustomDBus::getCustomDBus().implementGlobalInterface(path);
}},
    {"LicenseEntry",
     [](const std::string& path, Properties values) {
    std::string name{};
    std::string serialno{};
    dbus::LicenseEntryType type;
    dbus::LicenseEntryAuthorizationType authtype;
    uint32_t authdevno{};
    uint64_t exptime{};

    if (values.contains("name"))
    {
        name = std::get<std::string>(values.at("name"));
    }
    if (values.contains("serialNumber"))
    {
        serialno = std::get<std::string>(values.at("serialNumber"));
    }
    if (values.contains("type"))
    {
        type = std::get<dbus::LicenseEntryType>(values.at("type"));
    }
    if (values.contains("authorizationType"))
    {
        authtype = std::get<dbus::LicenseEntryAuthorizationType>(
            values.at("authorizationType"));
    }
    if (values.contains("expirationTime"))
    {
        exptime = std::get<uint64_t>(values.at("expirationTime"));
    }
    if (values.contains("authDeviceNumber"))
    {
        authdevno = std::get<uint32_t>(values.at("authDeviceNumber"));
    }

    pldm::dbus::CustomDBus::getCustomDBus().implementLicInterfaces(
        path, authdevno, name, serialno, exptime, type, authtype);
}},
    {"SoftWareVersion",
     [](const std::string& path, Properties values) {
    std::string version{};

    if (values.contains("version"))
    {
        version = std::get<std::string>(values.at("version"));
    }

    pldm::dbus::CustomDBus::getCustomDBus().setSoftwareVersion(path, version);
}}

};

std::pair<std::set<uint16_t>, std::set<uint16_t>>
    getEntityTypes(const fs::path& path)
{
    std::set<uint16_t> restoreTypes{};
    std::set<uint16_t> storeTypes{};

    if (!fs::exists(path) || fs::is_empty(path))
    {
        error("The file '{PATH}' does not exist or may be empty", "PATH", path);
        return std::make_pair(restoreTypes, storeTypes);
    }

    try
    {
        std::ifstream jsonFile(path);
        auto json = Json::parse(jsonFile);

        // define the default JSON as empty
        const std::set<uint16_t> empty{};
        const Json emptyjson{};
        auto restorePaths = json.value("restore", emptyjson);
        auto storePaths = json.value("store", emptyjson);
        auto restoreobjectPaths = restorePaths.value("entityTypes", empty);
        auto storeobjectPaths = storePaths.value("entityTypes", empty);

        std::ranges::transform(
            restoreobjectPaths,
            std::inserter(restoreTypes, restoreTypes.begin()),
            [](const auto& type) { return type; });
        std::ranges::transform(storeobjectPaths,
                               std::inserter(storeTypes, storeTypes.begin()),
                               [](const auto& type) { return type; });
    }
    catch (const std::exception& e)
    {
        error("Failed to parse config file '{PATH}': {ERROR}", "PATH", path,
              "ERROR", e);
    }

    return std::make_pair(restoreTypes, storeTypes);
}

void restoreDbusObj(HostPDRHandler* hostPDRHandler)
{
    if (!hostPDRHandler)
    {
        return;
    }

    auto entityTypes = getEntityTypes(DBUS_JSON_FILE);
    pldm::serialize::Serialize::getSerialize().setEntityTypes(
        entityTypes.second);

    if (!pldm::serialize::Serialize::getSerialize().deserialize())
    {
        return;
    }

    auto savedObjs = pldm::serialize::Serialize::getSerialize().getSavedObjs();

    for (auto& [type, objs] : savedObjs)
    {
        if (!entityTypes.first.contains(type))
        {
            continue;
        }

        for (auto& [path, entites] : objs)
        {
            auto& [num, id, obj] = entites;
            pldm_entity entity{type, num, id};
            hostPDRHandler->updateObjectPathMaps(path, entity);
            for (auto& [name, propertyValue] : obj)
            {
                if (!dBusInterfaceHandler.contains(name))
                {
                    error(
                        "PropertyName '{NAME}' is not in dBusInterfaceHandler",
                        "NAME", name);
                    continue;
                }
                dBusInterfaceHandler.at(name)(path, propertyValue);
            }
        }
    }
}

} // namespace deserialize
} // namespace pldm
