#include "event_parser.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

PHOSPHOR_LOG2_USING;

namespace pldm::responder::events
{
namespace fs = std::filesystem;
using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

const Json emptyJson{};
const std::vector<Json> emptyJsonList{};
const std::vector<std::string> emptyStringVec{};

const std::set<std::string_view> supportedDbusPropertyTypes = {
    "bool",     "uint8_t", "int16_t",  "uint16_t", "int32_t",
    "uint32_t", "int64_t", "uint64_t", "double",   "string"};

StateSensorHandler::StateSensorHandler(const std::string& dirPath)
{
    fs::path dir(dirPath);
    if (!fs::exists(dir) || fs::is_empty(dir))
    {
        error("Event config directory does not exist or empty, DIR={DIR_PATH}",
              "DIR_PATH", dirPath.c_str());
        return;
    }

    for (auto& file : fs::directory_iterator(dirPath))
    {
        std::ifstream jsonFile(file.path());

        auto data = Json::parse(jsonFile, nullptr, false);
        if (data.is_discarded())
        {
            error("Parsing Event state sensor JSON file failed, FILE={FILE}",
                  "FILE", file.path().c_str());
            continue;
        }

        auto entries = data.value("entries", emptyJsonList);
        for (const auto& entry : entries)
        {
            StateSensorEntry stateSensorEntry{};
            stateSensorEntry.containerId =
                static_cast<uint16_t>(entry.value("containerID", 0xFFFF));
            stateSensorEntry.entityType =
                static_cast<uint16_t>(entry.value("entityType", 0));
            stateSensorEntry.entityInstance =
                static_cast<uint16_t>(entry.value("entityInstance", 0));
            stateSensorEntry.sensorOffset =
                static_cast<uint8_t>(entry.value("sensorOffset", 0));
            stateSensorEntry.stateSetid =
                static_cast<uint16_t>(entry.value("stateSetId", 0));

            // container id is not found in the json
            stateSensorEntry.skipContainerCheck =
                (stateSensorEntry.containerId == 0xFFFF) ? true : false;

            pldm::utils::DBusMapping dbusInfo{};

            auto dbus = entry.value("dbus", emptyJson);
            dbusInfo.objectPath = dbus.value("object_path", "");
            dbusInfo.interface = dbus.value("interface", "");
            dbusInfo.propertyName = dbus.value("property_name", "");
            dbusInfo.propertyType = dbus.value("property_type", "");
            if (dbusInfo.objectPath.empty() || dbusInfo.interface.empty() ||
                dbusInfo.propertyName.empty() ||
                (supportedDbusPropertyTypes.find(dbusInfo.propertyType) ==
                 supportedDbusPropertyTypes.end()))
            {
                error(
                    "Invalid dbus config, OBJPATH= {OBJ_PATH} INTERFACE={INTF} PROPERTY_NAME={PROP_NAME} PROPERTY_TYPE={PROP_TYP}",
                    "OBJ_PATH", dbusInfo.objectPath.c_str(), "INTF",
                    dbusInfo.interface, "PROP_NAME", dbusInfo.propertyName,
                    "PROP_TYP", dbusInfo.propertyType);
                continue;
            }

            auto eventStates = entry.value("event_states", emptyJsonList);
            auto propertyValues = dbus.value("property_values", emptyJsonList);
            if ((eventStates.size() == 0) || (propertyValues.size() == 0) ||
                (eventStates.size() != propertyValues.size()))
            {
                error(
                    "Invalid event state JSON config, EVENT_STATE_SIZE={EVENT_STATE_SIZE} PROPERTY_VALUE_SIZE={PROP_VAL_SIZE}",
                    "EVENT_STATE_SIZE", eventStates.size(), "PROP_VAL_SIZE",
                    propertyValues.size());
                continue;
            }

            auto eventStateMap = mapStateToDBusVal(eventStates, propertyValues,
                                                   dbusInfo.propertyType);
            eventMap.emplace(
                stateSensorEntry,
                std::make_tuple(std::move(dbusInfo), std::move(eventStateMap)));
        }
    }
}

StateToDBusValue StateSensorHandler::mapStateToDBusVal(
    const Json& eventStates, const Json& propertyValues, std::string_view type)
{
    StateToDBusValue eventStateMap{};
    auto stateIt = eventStates.begin();
    auto propIt = propertyValues.begin();

    for (; stateIt != eventStates.end(); ++stateIt, ++propIt)
    {
        auto propValue = utils::jsonEntryToDbusVal(type, propIt.value());
        eventStateMap.emplace((*stateIt).get<uint8_t>(), std::move(propValue));
    }

    return eventStateMap;
}

int StateSensorHandler::eventAction(StateSensorEntry entry,
                                    pdr::EventState state)
{
    for (const auto& kv : eventMap)
    {
        if (kv.first.skipContainerCheck &&
            kv.first.entityType == entry.entityType &&
            kv.first.entityInstance == entry.entityInstance &&
            kv.first.stateSetid == entry.stateSetid &&
            kv.first.sensorOffset == entry.sensorOffset)
        {
            entry.skipContainerCheck = true;
            break;
        }
    }
    try
    {
        const auto& [dbusMapping, eventStateMap] = eventMap.at(entry);
        utils::PropertyValue propValue{};
        try
        {
            propValue = eventStateMap.at(state);
        }
        catch (const std::out_of_range& e)
        {
            error("Invalid event state {EVENT_STATE}  ERROR={ERR_EXCEP}",
                  "EVENT_STATE", static_cast<unsigned>(state), "ERR_EXCEP",
                  e.what());
            return PLDM_ERROR_INVALID_DATA;
        }

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, propValue);
        }
        catch (const std::exception& e)
        {
            error(
                "Error setting property, ERROR={ERR_EXCEP} PROPERTY={DBUS_PROP} INTERFACE={INTF} PATH = {OBJ_PATH}",
                "ERR_EXCEP", e.what(), "DBUS_PROP", dbusMapping.propertyName,
                "INTF", dbusMapping.interface, "OBJ_PATH",
                dbusMapping.objectPath.c_str());
            return PLDM_ERROR;
        }
    }
    catch (const std::out_of_range&)
    {
        // There is no BMC action for this PLDM event
        return PLDM_SUCCESS;
    }
    return PLDM_SUCCESS;
}

} // namespace pldm::responder::events
