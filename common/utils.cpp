#include "utils.hpp"

#include "libpldm/platform.h"

#include <libpldm/pdr.h>
#include <libpldm/pldm_types.h>
#include <stdio.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Logging/Create/client.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace utils
{

std::vector<std::vector<uint8_t>> findStateEffecterPDR(uint8_t /*tid*/,
                                                       uint16_t entityID,
                                                       uint16_t stateSetId,
                                                       const pldm_pdr* repo)
{
    uint8_t* outData = nullptr;
    uint32_t size{};
    const pldm_pdr_record* record{};
    std::vector<std::vector<uint8_t>> pdrs;
    try
    {
        do
        {
            record = pldm_pdr_find_record_by_type(repo, PLDM_STATE_EFFECTER_PDR,
                                                  record, &outData, &size);
            if (record)
            {
                auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(outData);
                auto compositeEffecterCount = pdr->composite_effecter_count;
                auto possible_states_start = pdr->possible_states;

                for (auto effecters = 0x00; effecters < compositeEffecterCount;
                     effecters++)
                {
                    auto possibleStates =
                        reinterpret_cast<state_effecter_possible_states*>(
                            possible_states_start);
                    auto setId = possibleStates->state_set_id;
                    auto possibleStateSize =
                        possibleStates->possible_states_size;

                    if (pdr->entity_type == entityID && setId == stateSetId)
                    {
                        std::vector<uint8_t> effecter_pdr(&outData[0],
                                                          &outData[size]);
                        pdrs.emplace_back(std::move(effecter_pdr));
                        break;
                    }
                    possible_states_start += possibleStateSize + sizeof(setId) +
                                             sizeof(possibleStateSize);
                }
            }

        } while (record);
    }
    catch (const std::exception& e)
    {
        error("Failed to obtain a record, error - {ERROR}", "ERROR", e);
    }

    return pdrs;
}

std::vector<std::vector<uint8_t>> findStateSensorPDR(uint8_t /*tid*/,
                                                     uint16_t entityID,
                                                     uint16_t stateSetId,
                                                     const pldm_pdr* repo)
{
    uint8_t* outData = nullptr;
    uint32_t size{};
    const pldm_pdr_record* record{};
    std::vector<std::vector<uint8_t>> pdrs;
    try
    {
        do
        {
            record = pldm_pdr_find_record_by_type(repo, PLDM_STATE_SENSOR_PDR,
                                                  record, &outData, &size);
            if (record)
            {
                auto pdr = reinterpret_cast<pldm_state_sensor_pdr*>(outData);
                auto compositeSensorCount = pdr->composite_sensor_count;
                auto possible_states_start = pdr->possible_states;

                for (auto sensors = 0x00; sensors < compositeSensorCount;
                     sensors++)
                {
                    auto possibleStates =
                        reinterpret_cast<state_sensor_possible_states*>(
                            possible_states_start);
                    auto setId = possibleStates->state_set_id;
                    auto possibleStateSize =
                        possibleStates->possible_states_size;

                    if (pdr->entity_type == entityID && setId == stateSetId)
                    {
                        std::vector<uint8_t> sensor_pdr(&outData[0],
                                                        &outData[size]);
                        pdrs.emplace_back(std::move(sensor_pdr));
                        break;
                    }
                    possible_states_start += possibleStateSize + sizeof(setId) +
                                             sizeof(possibleStateSize);
                }
            }

        } while (record);
    }
    catch (const std::exception& e)
    {
        error(
            "Failed to obtain a record with entity ID '{ENTITYID}', error - {ERROR}",
            "ENTITYID", entityID, "ERROR", e);
    }

    return pdrs;
}

uint8_t readHostEID()
{
    uint8_t eid{};
    std::ifstream eidFile{HOST_EID_PATH};
    if (!eidFile.good())
    {
        error("Failed to open remote terminus EID file at path '{PATH}'",
              "PATH", static_cast<std::string>(HOST_EID_PATH));
    }
    else
    {
        std::string eidStr;
        eidFile >> eidStr;
        if (!eidStr.empty())
        {
            eid = atoi(eidStr.c_str());
        }
        else
        {
            error("Remote terminus EID file was empty");
        }
    }

    return eid;
}

uint8_t getNumPadBytes(uint32_t data)
{
    uint8_t pad;
    pad = ((data % 4) ? (4 - data % 4) : 0);
    return pad;
} // end getNumPadBytes

bool uintToDate(uint64_t data, uint16_t* year, uint8_t* month, uint8_t* day,
                uint8_t* hour, uint8_t* min, uint8_t* sec)
{
    constexpr uint64_t max_data = 29991231115959;
    constexpr uint64_t min_data = 19700101000000;
    if (data < min_data || data > max_data)
    {
        return false;
    }

    *year = data / 10000000000;
    data = data % 10000000000;
    *month = data / 100000000;
    data = data % 100000000;
    *day = data / 1000000;
    data = data % 1000000;
    *hour = data / 10000;
    data = data % 10000;
    *min = data / 100;
    *sec = data % 100;

    return true;
}

std::optional<std::vector<set_effecter_state_field>>
    parseEffecterData(const std::vector<uint8_t>& effecterData,
                      uint8_t effecterCount)
{
    std::vector<set_effecter_state_field> stateField;

    if (effecterData.size() != effecterCount * 2)
    {
        return std::nullopt;
    }

    for (uint8_t i = 0; i < effecterCount; ++i)
    {
        uint8_t set_request = effecterData[i * 2] == PLDM_REQUEST_SET
                                  ? PLDM_REQUEST_SET
                                  : PLDM_NO_CHANGE;
        set_effecter_state_field filed{set_request, effecterData[i * 2 + 1]};
        stateField.emplace_back(std::move(filed));
    }

    return std::make_optional(std::move(stateField));
}

std::string DBusHandler::getService(const char* path,
                                    const char* interface) const
{
    using DbusInterfaceList = std::vector<std::string>;
    std::map<std::string, std::vector<std::string>> mapperResponse;
    auto& bus = DBusHandler::getBus();

    auto mapper = bus.new_method_call(ObjectMapper::default_service,
                                      ObjectMapper::instance_path,
                                      ObjectMapper::interface, "GetObject");

    if (interface)
    {
        mapper.append(path, DbusInterfaceList({interface}));
    }
    else
    {
        mapper.append(path, DbusInterfaceList({}));
    }

    auto mapperResponseMsg = bus.call(mapper, dbusTimeout);
    mapperResponseMsg.read(mapperResponse);
    return mapperResponse.begin()->first;
}

GetSubTreeResponse
    DBusHandler::getSubtree(const std::string& searchPath, int depth,
                            const std::vector<std::string>& ifaceList) const
{
    auto& bus = pldm::utils::DBusHandler::getBus();
    auto method = bus.new_method_call(ObjectMapper::default_service,
                                      ObjectMapper::instance_path,
                                      ObjectMapper::interface, "GetSubTree");
    method.append(searchPath, depth, ifaceList);
    auto reply = bus.call(method, dbusTimeout);
    GetSubTreeResponse response;
    reply.read(response);
    return response;
}

GetSubTreePathsResponse DBusHandler::getSubTreePaths(
    const std::string& objectPath, int depth,
    const std::vector<std::string>& ifaceList) const
{
    std::vector<std::string> paths;
    auto& bus = pldm::utils::DBusHandler::getBus();
    auto method = bus.new_method_call(
        ObjectMapper::default_service, ObjectMapper::instance_path,
        ObjectMapper::interface, "GetSubTreePaths");
    method.append(objectPath, depth, ifaceList);
    auto reply = bus.call(method, dbusTimeout);

    reply.read(paths);
    return paths;
}

void reportError(const char* errorMsg)
{
    auto& bus = pldm::utils::DBusHandler::getBus();
    using LoggingCreate =
        sdbusplus::client::xyz::openbmc_project::logging::Create<>;
    try
    {
        using namespace sdbusplus::xyz::openbmc_project::Logging::server;
        auto severity =
            sdbusplus::xyz::openbmc_project::Logging::server::convertForMessage(
                sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level::
                    Error);
        auto method = bus.new_method_call(LoggingCreate::default_service,
                                          LoggingCreate::instance_path,
                                          LoggingCreate::interface, "Create");

        std::map<std::string, std::string> addlData{};
        method.append(errorMsg, severity, addlData);
        bus.call_noreply(method, dbusTimeout);
    }
    catch (const std::exception& e)
    {
        error(
            "Failed to do dbus call for creating error log for '{ERRMSG}' at path '{PATH}' and interface '{INTERFACE}', error - {ERROR}",
            "ERRMSG", errorMsg, "PATH", LoggingCreate::instance_path,
            "INTERFACE", LoggingCreate::interface, "ERROR", e);
    }
}

void DBusHandler::setDbusProperty(const DBusMapping& dBusMap,
                                  const PropertyValue& value) const
{
    auto setDbusValue = [&dBusMap, this](const auto& variant) {
        auto& bus = getBus();
        auto service = getService(dBusMap.objectPath.c_str(),
                                  dBusMap.interface.c_str());
        auto method = bus.new_method_call(
            service.c_str(), dBusMap.objectPath.c_str(), dbusProperties, "Set");
        method.append(dBusMap.interface.c_str(), dBusMap.propertyName.c_str(),
                      variant);
        bus.call_noreply(method, dbusTimeout);
    };

    if (dBusMap.propertyType == "uint8_t")
    {
        std::variant<uint8_t> v = std::get<uint8_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "bool")
    {
        std::variant<bool> v = std::get<bool>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "int16_t")
    {
        std::variant<int16_t> v = std::get<int16_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "uint16_t")
    {
        std::variant<uint16_t> v = std::get<uint16_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "int32_t")
    {
        std::variant<int32_t> v = std::get<int32_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "uint32_t")
    {
        std::variant<uint32_t> v = std::get<uint32_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "int64_t")
    {
        std::variant<int64_t> v = std::get<int64_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "uint64_t")
    {
        std::variant<uint64_t> v = std::get<uint64_t>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "double")
    {
        std::variant<double> v = std::get<double>(value);
        setDbusValue(v);
    }
    else if (dBusMap.propertyType == "string")
    {
        std::variant<std::string> v = std::get<std::string>(value);
        setDbusValue(v);
    }
    else
    {
        error("Unsupported property type '{TYPE}'", "TYPE",
              dBusMap.propertyType);
        throw std::invalid_argument("UnSupported Dbus Type");
    }
}

PropertyValue DBusHandler::getDbusPropertyVariant(
    const char* objPath, const char* dbusProp, const char* dbusInterface) const
{
    auto& bus = DBusHandler::getBus();
    auto service = getService(objPath, dbusInterface);
    auto method = bus.new_method_call(service.c_str(), objPath, dbusProperties,
                                      "Get");
    method.append(dbusInterface, dbusProp);
    return bus.call(method, dbusTimeout).unpack<PropertyValue>();
}

ObjectValueTree DBusHandler::getManagedObj(const char* service,
                                           const char* rootPath)
{
    auto& bus = DBusHandler::getBus();
    auto method = bus.new_method_call(service, rootPath,
                                      "org.freedesktop.DBus.ObjectManager",
                                      "GetManagedObjects");
    return bus.call(method).unpack<ObjectValueTree>();
}

PropertyMap
    DBusHandler::getDbusPropertiesVariant(const char* serviceName,
                                          const char* objPath,
                                          const char* dbusInterface) const
{
    auto& bus = DBusHandler::getBus();
    auto method = bus.new_method_call(serviceName, objPath, dbusProperties,
                                      "GetAll");
    method.append(dbusInterface);
    return bus.call(method, dbusTimeout).unpack<PropertyMap>();
}

PropertyValue jsonEntryToDbusVal(std::string_view type,
                                 const nlohmann::json& value)
{
    PropertyValue propValue{};
    if (type == "uint8_t")
    {
        propValue = static_cast<uint8_t>(value);
    }
    else if (type == "uint16_t")
    {
        propValue = static_cast<uint16_t>(value);
    }
    else if (type == "uint32_t")
    {
        propValue = static_cast<uint32_t>(value);
    }
    else if (type == "uint64_t")
    {
        propValue = static_cast<uint64_t>(value);
    }
    else if (type == "int16_t")
    {
        propValue = static_cast<int16_t>(value);
    }
    else if (type == "int32_t")
    {
        propValue = static_cast<int32_t>(value);
    }
    else if (type == "int64_t")
    {
        propValue = static_cast<int64_t>(value);
    }
    else if (type == "bool")
    {
        propValue = static_cast<bool>(value);
    }
    else if (type == "double")
    {
        propValue = static_cast<double>(value);
    }
    else if (type == "string")
    {
        propValue = static_cast<std::string>(value);
    }
    else
    {
        error("Unknown D-Bus property type '{TYPE}'", "TYPE", type);
    }

    return propValue;
}

uint16_t findStateEffecterId(const pldm_pdr* pdrRepo, uint16_t entityType,
                             uint16_t entityInstance, uint16_t containerId,
                             uint16_t stateSetId, bool localOrRemote)
{
    uint8_t* pdrData = nullptr;
    uint32_t pdrSize{};
    const pldm_pdr_record* record{};
    do
    {
        record = pldm_pdr_find_record_by_type(pdrRepo, PLDM_STATE_EFFECTER_PDR,
                                              record, &pdrData, &pdrSize);
        if (record && (localOrRemote ^ pldm_pdr_record_is_remote(record)))
        {
            auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(pdrData);
            auto compositeEffecterCount = pdr->composite_effecter_count;
            auto possible_states_start = pdr->possible_states;

            for (auto effecters = 0x00; effecters < compositeEffecterCount;
                 effecters++)
            {
                auto possibleStates =
                    reinterpret_cast<state_effecter_possible_states*>(
                        possible_states_start);
                auto setId = possibleStates->state_set_id;
                auto possibleStateSize = possibleStates->possible_states_size;

                if (entityType == pdr->entity_type &&
                    entityInstance == pdr->entity_instance &&
                    containerId == pdr->container_id && stateSetId == setId)
                {
                    return pdr->effecter_id;
                }
                possible_states_start += possibleStateSize + sizeof(setId) +
                                         sizeof(possibleStateSize);
            }
        }
    } while (record);

    return PLDM_INVALID_EFFECTER_ID;
}

int emitStateSensorEventSignal(uint8_t tid, uint16_t sensorId,
                               uint8_t sensorOffset, uint8_t eventState,
                               uint8_t previousEventState)
{
    try
    {
        auto& bus = DBusHandler::getBus();
        auto msg = bus.new_signal("/xyz/openbmc_project/pldm",
                                  "xyz.openbmc_project.PLDM.Event",
                                  "StateSensorEvent");
        msg.append(tid, sensorId, sensorOffset, eventState, previousEventState);

        msg.signal_send();
    }
    catch (const std::exception& e)
    {
        error("Failed to emit pldm event signal, error - {ERROR}", "ERROR", e);
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

uint16_t findStateSensorId(const pldm_pdr* pdrRepo, uint8_t tid,
                           uint16_t entityType, uint16_t entityInstance,
                           uint16_t containerId, uint16_t stateSetId)
{
    auto pdrs = findStateSensorPDR(tid, entityType, stateSetId, pdrRepo);
    for (auto pdr : pdrs)
    {
        auto sensorPdr = reinterpret_cast<pldm_state_sensor_pdr*>(pdr.data());
        auto compositeSensorCount = sensorPdr->composite_sensor_count;
        auto possible_states_start = sensorPdr->possible_states;

        for (auto sensors = 0x00; sensors < compositeSensorCount; sensors++)
        {
            auto possibleStates =
                reinterpret_cast<state_sensor_possible_states*>(
                    possible_states_start);
            auto setId = possibleStates->state_set_id;
            auto possibleStateSize = possibleStates->possible_states_size;
            if (entityType == sensorPdr->entity_type &&
                entityInstance == sensorPdr->entity_instance &&
                stateSetId == setId && containerId == sensorPdr->container_id)
            {
                return sensorPdr->sensor_id;
            }
            possible_states_start += possibleStateSize + sizeof(setId) +
                                     sizeof(possibleStateSize);
        }
    }
    return PLDM_INVALID_EFFECTER_ID;
}

void printBuffer(bool isTx, const std::vector<uint8_t>& buffer)
{
    if (buffer.empty())
    {
        std::cerr << "Buffer is empty" << std::endl;
        return;
    }
    else if (buffer[2] == PLDM_GET_PDR ||
             buffer[2] == PLDM_PLATFORM_EVENT_MESSAGE)
    {
        std::cout << (isTx ? "Tx: " : "Rx: ");

        std::ranges::for_each(buffer, [](uint8_t byte) {
            std::cout << std::format("{:02x} ", byte);
        });

        std::cout << std::endl;
    }
}

std::string toString(const struct variable_field& var)
{
    if (var.ptr == nullptr || !var.length)
    {
        return "";
    }

    std::string str(reinterpret_cast<const char*>(var.ptr), var.length);
    std::replace_if(str.begin(), str.end(),
                    [](const char& c) { return !isprint(c); }, ' ');
    return str;
}

std::vector<std::string> split(std::string_view srcStr, std::string_view delim,
                               std::string_view trimStr)
{
    std::vector<std::string> out;
    size_t start;
    size_t end = 0;

    while ((start = srcStr.find_first_not_of(delim, end)) != std::string::npos)
    {
        end = srcStr.find(delim, start);
        std::string_view dstStr = srcStr.substr(start, end - start);
        if (!trimStr.empty())
        {
            dstStr.remove_prefix(dstStr.find_first_not_of(trimStr));
            dstStr.remove_suffix(dstStr.size() - 1 -
                                 dstStr.find_last_not_of(trimStr));
        }

        if (!dstStr.empty())
        {
            out.push_back(std::string(dstStr));
        }
    }

    return out;
}

std::string getCurrentSystemTime()
{
    const auto zonedTime{std::chrono::zoned_time{
        std::chrono::current_zone(), std::chrono::system_clock::now()}};
    return std::format("{:%F %Z %T}", zonedTime);
}

bool checkForFruPresence(const std::string& objPath)
{
    bool isPresent = false;
    static constexpr auto presentInterface =
        "xyz.openbmc_project.Inventory.Item";
    static constexpr auto presentProperty = "Present";
    try
    {
        auto propVal = pldm::utils::DBusHandler().getDbusPropertyVariant(
            objPath.c_str(), presentProperty, presentInterface);
        isPresent = std::get<bool>(propVal);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        error("Failed to check for FRU presence at {PATH}, error - {ERROR}",
              "PATH", objPath, "ERROR", e);
    }
    return isPresent;
}

bool checkIfLogicalBitSet(const uint16_t& containerId)
{
    return !(containerId & 0x8000);
}

void setFruPresence(const std::string& fruObjPath, bool present)
{
    pldm::utils::PropertyValue value{present};
    pldm::utils::DBusMapping dbusMapping;
    dbusMapping.objectPath = fruObjPath;
    dbusMapping.interface = "xyz.openbmc_project.Inventory.Item";
    dbusMapping.propertyName = "Present";
    dbusMapping.propertyType = "bool";
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error(
            "Failed to set the present property on path '{PATH}', error - {ERROR}.",
            "PATH", fruObjPath, "ERROR", e);
    }
}

std::vector<std::vector<pldm::pdr::Pdr_t>>
    getStateSensorPDRsByType(uint8_t /*tid*/, uint16_t entityType,
                             const pldm_pdr* repo)
{
    uint8_t* outData = nullptr;
    uint32_t size{};
    const pldm_pdr_record* record{};
    std::vector<std::vector<uint8_t>> pdrs;
    do
    {
        record = pldm_pdr_find_record_by_type(repo, PLDM_STATE_SENSOR_PDR,
                                              record, &outData, &size);

        if (record)
        {
            auto pdr = reinterpret_cast<pldm_state_sensor_pdr*>(outData);
            if (pdr)
            {
                auto compositeSensorCount = pdr->composite_sensor_count;
                auto possible_states_start = pdr->possible_states;

                for (auto sensors = 0x00; sensors < compositeSensorCount;
                     sensors++)
                {
                    auto possibleStates =
                        reinterpret_cast<state_sensor_possible_states*>(
                            possible_states_start);
                    auto setId = possibleStates->state_set_id;
                    auto possibleStateSize =
                        possibleStates->possible_states_size;

                    if (pdr->entity_type == entityType)
                    {
                        std::vector<uint8_t> sensor_pdr(&outData[0],
                                                        &outData[size]);
                        pdrs.emplace_back(std::move(sensor_pdr));
                        break;
                    }
                    possible_states_start += possibleStateSize + sizeof(setId) +
                                             sizeof(possibleStateSize);
                }
            }
        }

    } while (record);

    return pdrs;
}

std::vector<pldm::pdr::SensorID> findSensorIds(const pldm_pdr* pdrRepo,
                                               uint8_t tid, uint16_t entityType,
                                               uint16_t entityInstance,
                                               uint16_t containerId)
{
    std::vector<uint16_t> sensorIDs;

    auto pdrs = getStateSensorPDRsByType(tid, entityType, pdrRepo);
    for (const auto& pdr : pdrs)
    {
        auto sensorPdr =
            reinterpret_cast<const pldm_state_sensor_pdr*>(pdr.data());
        if (sensorPdr)
        {
            auto compositeSensorCount = sensorPdr->composite_sensor_count;
            auto possible_states_start = sensorPdr->possible_states;

            for (auto sensors = 0x00; sensors < compositeSensorCount; sensors++)
            {
                auto possibleStates =
                    reinterpret_cast<const state_sensor_possible_states*>(
                        possible_states_start);
                auto possibleStateSize = possibleStates->possible_states_size;
                if (entityType == sensorPdr->entity_type &&
                    entityInstance == sensorPdr->entity_instance &&
                    containerId == sensorPdr->container_id)
                {
                    uint16_t id = sensorPdr->sensor_id;
                    sensorIDs.emplace_back(std::move(id));
                }
                possible_states_start += possibleStateSize +
                                         sizeof(possibleStates->state_set_id) +
                                         sizeof(possibleStateSize);
            }
        }
    }
    return sensorIDs;
}

std::vector<std::vector<pldm::pdr::Pdr_t>>
    getStateEffecterPDRsByType(uint8_t /*tid*/, uint16_t entityType,
                               const pldm_pdr* repo)
{
    uint8_t* outData = nullptr;
    uint32_t size{};
    const pldm_pdr_record* record{};
    std::vector<std::vector<uint8_t>> pdrs;

    do
    {
        record = pldm_pdr_find_record_by_type(repo, PLDM_STATE_EFFECTER_PDR,
                                              record, &outData, &size);

        if (record)
        {
            auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(outData);
            if (pdr)
            {
                auto compositeEffecterCount = pdr->composite_effecter_count;
                auto possible_states_start = pdr->possible_states;

                for (auto effecters = 0x00; effecters < compositeEffecterCount;
                     effecters++)
                {
                    auto possibleStates =
                        reinterpret_cast<state_effecter_possible_states*>(
                            possible_states_start);
                    auto setId = possibleStates->state_set_id;
                    auto possibleStateSize =
                        possibleStates->possible_states_size;

                    if (pdr->entity_type == entityType)
                    {
                        std::vector<uint8_t> effecter_pdr(&outData[0],
                                                          &outData[size]);
                        pdrs.emplace_back(std::move(effecter_pdr));
                        break;
                    }
                    possible_states_start += possibleStateSize + sizeof(setId) +
                                             sizeof(possibleStateSize);
                }
            }
        }
    } while (record);

    return pdrs;
}

std::vector<pldm::pdr::EffecterID>
    findEffecterIds(const pldm_pdr* pdrRepo, uint8_t tid, uint16_t entityType,
                    uint16_t entityInstance, uint16_t containerId)
{
    std::vector<uint16_t> effecterIDs;

    auto pdrs = getStateEffecterPDRsByType(tid, entityType, pdrRepo);
    for (const auto& pdr : pdrs)
    {
        auto effecterPdr =
            reinterpret_cast<const pldm_state_effecter_pdr*>(pdr.data());
        if (effecterPdr)
        {
            auto compositeEffecterCount = effecterPdr->composite_effecter_count;
            auto possible_states_start = effecterPdr->possible_states;

            for (auto effecters = 0x00; effecters < compositeEffecterCount;
                 effecters++)
            {
                auto possibleStates =
                    reinterpret_cast<const state_effecter_possible_states*>(
                        possible_states_start);
                auto possibleStateSize = possibleStates->possible_states_size;
                if (entityType == effecterPdr->entity_type &&
                    entityInstance == effecterPdr->entity_instance &&
                    containerId == effecterPdr->container_id)
                {
                    uint16_t id = effecterPdr->effecter_id;
                    effecterIDs.emplace_back(std::move(id));
                }
                possible_states_start += possibleStateSize +
                                         sizeof(possibleStates->state_set_id) +
                                         sizeof(possibleStateSize);
            }
        }
    }
    return effecterIDs;
}

std::string getBiosAttrValue(const std::string& dbusAttrName)
{
    constexpr auto biosConfigPath = "/xyz/openbmc_project/bios_config/manager";
    constexpr auto biosConfigIntf = "xyz.openbmc_project.BIOSConfig.Manager";

    std::string var1;
    std::variant<std::string> var2;
    std::variant<std::string> var3;

    auto& bus = DBusHandler::getBus();
    try
    {
        auto service = pldm::utils::DBusHandler().getService(biosConfigPath,
                                                             biosConfigIntf);
        auto method = bus.new_method_call(
            service.c_str(), biosConfigPath,
            "xyz.openbmc_project.BIOSConfig.Manager", "GetAttribute");
        method.append(dbusAttrName);
        auto reply = bus.call(method, dbusTimeout);
        reply.read(var1, var2, var3);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        info(
            "Error getting the bios attribute ERROR={ERR_EXCEP} ATTRIBUTE={BIOS_ATTR}",
            "ERR_EXCEP", e, "BIOS_ATTR", dbusAttrName);
        return {};
    }

    return std::get<std::string>(var2);
}

void setBiosAttr(const BiosAttributeList& biosAttrList)
{
    static constexpr auto SYSTEMD_PROPERTY_INTERFACE =
        "org.freedesktop.DBus.Properties";
    constexpr auto biosConfigPath = "/xyz/openbmc_project/bios_config/manager";
    constexpr auto biosConfigIntf = "xyz.openbmc_project.BIOSConfig.Manager";

    constexpr auto dbusAttrType =
        "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration";
    for (const auto& [dbusAttrName, biosAttrStr] : biosAttrList)
    {
        using PendingAttributesType = std::vector<std::pair<
            std::string, std::tuple<std::string, std::variant<std::string>>>>;
        PendingAttributesType pendingAttributes;
        pendingAttributes.emplace_back(std::make_pair(
            dbusAttrName, std::make_tuple(dbusAttrType, biosAttrStr)));

        auto& bus = DBusHandler::getBus();
        try
        {
            auto service = pldm::utils::DBusHandler().getService(
                biosConfigPath, biosConfigIntf);
            auto method = bus.new_method_call(service.c_str(), biosConfigPath,
                                              SYSTEMD_PROPERTY_INTERFACE,
                                              "Set");
            method.append(
                biosConfigIntf, "PendingAttributes",
                std::variant<PendingAttributesType>(pendingAttributes));
            bus.call_noreply(method, dbusTimeout);
        }
        catch (const sdbusplus::exception::SdBusError& e)
        {
            info(
                "Error setting the bios attribute ERROR = {ERR_EXCEP} ATTRIBUTE= {DBUS_ATTR} ATTRIBUTE VALUE={BIOS_ATTR}",
                "ERR_EXCEP", e, "DBUS_ATTR", dbusAttrName.c_str(), "BIOS_ATTR",
                biosAttrStr.c_str());
            return;
        }
    }
}

} // namespace utils
} // namespace pldm
