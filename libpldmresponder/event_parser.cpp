#include "event_parser.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <phosphor-logging/lg2.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

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
        //FilePathError
        lg2::error("Event config directory does not exist or empty, DIR={KEY0}", "KEY0", dirPath.c_str());
        return;
    }

    for (auto& file : fs::directory_iterator(dirPath))
    {
        std::ifstream jsonFile(file.path());

        auto data = Json::parse(jsonFile, nullptr, false);
        if (data.is_discarded())
        {
            //FilePathError
            lg2::error("Parsing Event state sensor JSON file failed, FILE={KEY0}", "KEY0", file.path().c_str());
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
            { //FilePathError
                lg2::error("Invalid dbus config, OBJPATH= {KEY0} INTERFACE={KEY1} PROPERTY_NAME={KEY2} PROPERTY_TYPE={KEY3}", "KEY0", dbusInfo.objectPath.c_str(), "KEY1", dbusInfo.interface, "KEY2", dbusInfo.propertyName, "KEY3", dbusInfo.propertyType);
                continue;
            }

            auto eventStates = entry.value("event_states", emptyJsonList);
            auto propertyValues = dbus.value("property_values", emptyJsonList);
            if ((eventStates.size() == 0) || (propertyValues.size() == 0) ||
                (eventStates.size() != propertyValues.size()))
            { 
                lg2::error("Invalid event state JSON config, EVENT_STATE_SIZE={KEY0} PROPERTY_VALUE_SIZE={KEY1}", "KEY0", eventStates.size(), "KEY1", propertyValues.size());
                    
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
            lg2::error("Invalid event state {KEY0}", "KEY0", unsigned(state));
            return PLDM_ERROR_INVALID_DATA;
        }

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, propValue);
        }
        catch (const std::exception& e)
        {
            lg2::error( "Error setting property, ERROR={KEY0} PROPERTY={KEY1} INTERFACE={KEY2} PATH = {KEY3}", "KEY0", e.what(), "KEY1", dbusMapping.propertyName , "KEY2", dbusMapping.interface, "KEY3", dbusMapping.objectPath.c_str());
            return PLDM_ERROR;
        }
    }
    catch (const std::out_of_range& e)
    {
        // There is no BMC action for this PLDM event
        return PLDM_SUCCESS;
    }
    return PLDM_SUCCESS;
}

} // namespace pldm::responder::events
