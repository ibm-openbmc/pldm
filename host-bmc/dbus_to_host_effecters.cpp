#include "dbus_to_host_effecters.hpp"

#include "libpldm/pdr.h"
#include "libpldm/platform.h"
#include "libpldm/pldm.h"

#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/State/OperatingSystem/Status/server.hpp>

#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <iostream>

using namespace pldm::utils;

namespace pldm
{
namespace host_effecters
{

using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

constexpr auto hostEffecterJson = "dbus_to_host_effecter.json";

void HostEffecterParser::populatePropVals(
    const Json& dBusValues, std::vector<PropertyValue>& propertyValues,
    const std::string& propertyType)

{
    for (const auto& elem : dBusValues)
    {
        auto value = jsonEntryToDbusVal(propertyType, elem);
        propertyValues.emplace_back(value);
    }
}

void HostEffecterParser::parseEffecterJson(const std::string& jsonPath)
{
    fs::path jsonDir(jsonPath);
    if (!fs::exists(jsonDir) || fs::is_empty(jsonDir))
    {
        lg2::error("lg2 Host Effecter json path does not exist, DIR = {KEY0}", "KEY0", jsonPath.c_str());
                
        return;
    }

    fs::path jsonFilePath = jsonDir / hostEffecterJson;
    if (!fs::exists(jsonFilePath))
    {
        //FilePathError 
        lg2::error("lg2 json does not exist, PATH = {KEY0}", "KEY0", jsonFilePath.c_str());
        throw InternalFailure();
    }

    std::ifstream jsonFile(jsonFilePath);
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        //FilePathError 
        lg2::error("lg2 Parsing json file failed, FILE = {KEY0}", "KEY0", jsonFilePath.c_str());
        throw InternalFailure();
    }
    const Json empty{};
    const std::vector<Json> emptyList{};

    auto entries = data.value("entries", emptyList);
    for (const auto& entry : entries)
    {
        EffecterInfo effecterInfo;
        effecterInfo.mctpEid = entry.value("mctp_eid", 0xFF);
        auto jsonEffecterInfo = entry.value("effecter_info", empty);
        auto effecterId =
            jsonEffecterInfo.value("effecterID", PLDM_INVALID_EFFECTER_ID);
        effecterInfo.containerId = jsonEffecterInfo.value("containerID", 0);
        effecterInfo.entityType = jsonEffecterInfo.value("entityType", 0);
        effecterInfo.entityInstance =
            jsonEffecterInfo.value("entityInstance", 0);
        effecterInfo.compEffecterCnt =
            jsonEffecterInfo.value("compositeEffecterCount", 0);
        auto effecters = entry.value("effecters", emptyList);
        for (const auto& effecter : effecters)
        {
            DBusEffecterMapping dbusInfo{};
            auto jsonDbusInfo = effecter.value("dbus_info", empty);
            dbusInfo.dbusMap.objectPath = jsonDbusInfo.value("object_path", "");
            dbusInfo.dbusMap.interface = jsonDbusInfo.value("interface", "");
            dbusInfo.dbusMap.propertyName =
                jsonDbusInfo.value("property_name", "");
            dbusInfo.dbusMap.propertyType =
                jsonDbusInfo.value("property_type", "");
            Json propertyValues = jsonDbusInfo["property_values"];

            populatePropVals(propertyValues, dbusInfo.propertyValues,
                             dbusInfo.dbusMap.propertyType);

            const std::vector<uint8_t> emptyStates{};
            auto state = effecter.value("state", empty);
            dbusInfo.state.stateSetId = state.value("id", 0);
            auto states = state.value("state_values", emptyStates);
            if (dbusInfo.propertyValues.size() != states.size())
            {
                //FilePathError 
                lg2::error("lg2 Number of states do not match with number of D-Bus property values in the json. Object path {KEY0} and property {KEY1}  will not be monitored", "KEY0", dbusInfo.dbusMap.objectPath.c_str(), "KEY1", dbusInfo.dbusMap.propertyName);
             
                continue;
            }
            for (const auto& s : states)
            {
                dbusInfo.state.states.emplace_back(s);
            }

            auto effecterInfoIndex = hostEffecterInfo.size();
            auto dbusInfoIndex = effecterInfo.dbusInfo.size();
            createHostEffecterMatch(
                dbusInfo.dbusMap.objectPath, dbusInfo.dbusMap.interface,
                effecterInfoIndex, dbusInfoIndex, effecterId);
            effecterInfo.dbusInfo.emplace_back(std::move(dbusInfo));
        }
        hostEffecterInfo.emplace_back(std::move(effecterInfo));
    }
}

void HostEffecterParser::processHostEffecterChangeNotification(
    const DbusChgHostEffecterProps& chProperties, size_t effecterInfoIndex,
    size_t dbusInfoIndex, uint16_t effecterId)
{
    const auto& propertyName = hostEffecterInfo[effecterInfoIndex]
                                   .dbusInfo[dbusInfoIndex]
                                   .dbusMap.propertyName;

    const auto& it = chProperties.find(propertyName);

    if (it == chProperties.end())
    {
        return;
    }

    if (effecterId == PLDM_INVALID_EFFECTER_ID)
    {
        constexpr auto localOrRemote = false;
        effecterId = findStateEffecterId(
            pdrRepo, hostEffecterInfo[effecterInfoIndex].entityType,
            hostEffecterInfo[effecterInfoIndex].entityInstance,
            hostEffecterInfo[effecterInfoIndex].containerId,
            hostEffecterInfo[effecterInfoIndex]
                .dbusInfo[dbusInfoIndex]
                .state.stateSetId,
            localOrRemote);
        if (effecterId == PLDM_INVALID_EFFECTER_ID)
        {
            lg2::error("lg2 Effecter id not found in pdr repo");
            return;
        }
    }
    constexpr auto hostStateInterface =
        "xyz.openbmc_project.State.Boot.Progress";
    constexpr auto hostStatePath = "/xyz/openbmc_project/state/host0";

    try
    {
        auto propVal = dbusHandler->getDbusPropertyVariant(
            hostStatePath, "BootProgress", hostStateInterface);
        const auto& currHostState = std::get<std::string>(propVal);
        if ((currHostState != "xyz.openbmc_project.State.Boot.Progress."
                              "ProgressStages.SystemInitComplete") &&
            (currHostState != "xyz.openbmc_project.State.Boot.Progress."
                              "ProgressStages.OSRunning") &&
            (currHostState != "xyz.openbmc_project.State.Boot.Progress."
                              "ProgressStages.OSStart") &&
            (currHostState != "xyz.openbmc_project.State.Boot.Progress."
                              "ProgressStages.SystemSetup"))
        {
            lg2::info("lg2 Host is not up. Current host state: {KEY0}", "KEY0", currHostState.c_str());
        
            return;
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        lg2::error("lg2 Error in getting current host state. Will still continue to set the host effecter");
    
    }
    uint8_t newState{};
    try
    {
        newState =
            findNewStateValue(effecterInfoIndex, dbusInfoIndex, it->second);
    }
    catch (const std::out_of_range& e)
    {
        lg2::info("lg2 new state not found in json");
                
        return;
    }

    std::vector<set_effecter_state_field> stateField;
    for (uint8_t i = 0; i < hostEffecterInfo[effecterInfoIndex].compEffecterCnt;
         i++)
    {
        if (i == dbusInfoIndex)
        {
            stateField.push_back({PLDM_REQUEST_SET, newState});
        }
        else
        {
            stateField.push_back({PLDM_NO_CHANGE, 0});
        }
    }
    int rc{};
    try
    {
        rc = setHostStateEffecter(effecterInfoIndex, stateField, effecterId);
    }
    catch (const std::runtime_error& e)
    {
        lg2::error("lg2 Could not set host state effecter");
        return;
    }
    if (rc != PLDM_SUCCESS)
    {
        lg2::error("lg2 Could not set the host state effecter, rc= {KEY0}", "KEY0", rc);
        
    }
}

uint8_t
    HostEffecterParser::findNewStateValue(size_t effecterInfoIndex,
                                          size_t dbusInfoIndex,
                                          const PropertyValue& propertyValue)
{
    const auto& propValues = hostEffecterInfo[effecterInfoIndex]
                                 .dbusInfo[dbusInfoIndex]
                                 .propertyValues;
    auto it = std::find(propValues.begin(), propValues.end(), propertyValue);
    uint8_t newState{};
    if (it != propValues.end())
    {
        auto index = std::distance(propValues.begin(), it);
        newState = hostEffecterInfo[effecterInfoIndex]
                       .dbusInfo[dbusInfoIndex]
                       .state.states[index];
    }
    else
    {
        throw std::out_of_range("new state not found in json");
    }
    return newState;
}

int HostEffecterParser::sendSetStateEffecterStates(
    uint8_t mctpEid, uint16_t effecterId, uint8_t compEffCnt,
    std::vector<set_effecter_state_field>& stateField,
    std::function<bool(bool)> callBack, bool value)
{
    auto instanceId = requester->getInstanceId(mctpEid);

    std::vector<uint8_t> requestMsg(
        sizeof(pldm_msg_hdr) + sizeof(effecterId) + sizeof(compEffCnt) +
            sizeof(set_effecter_state_field) * compEffCnt,
        0);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_set_state_effecter_states_req(
        instanceId, effecterId, compEffCnt, stateField.data(), request);

    if (rc != PLDM_SUCCESS)
    {
        lg2::error("lg2 Message encode SetStateEffecterStates failure. PLDM error code = {KEY0}", "KEY0", lg2::hex, rc);
           // << std::hex << std::showbase << rc << "\n";
        requester->markFree(mctpEid, instanceId);
        return rc;
    }

    auto setStateEffecterStatesRespHandler = [=](mctp_eid_t /*eid*/,
                                                 const pldm_msg* response,
                                                 size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            lg2::error("lg2 Failed to receive response for setStateEffecterStates command");
            return;
        }
        uint8_t completionCode{};
        auto rc = decode_set_state_effecter_states_resp(response, respMsgLen,
                                                        &completionCode);
        if (rc)
        {
            lg2::error("lg2 Failed to decode setStateEffecterStates response, effecter Id : {KEY0}, rc = {KEY1}", "KEY0", effecterId, "KEY1", rc);
              
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.SetHostEffecterFailed",
                pldm::PelSeverity::ERROR);
        }
        if (completionCode)
        {
            auto cc = unsigned(completionCode);
            lg2::error("Failed to set a Host effecter, effecter Id : {KEY0}, cc = {KEY1}", "KEY0", effecterId, "KEY1", cc);
            
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.SetHostEffecterFailed",
                pldm::PelSeverity::ERROR);
        }
        else
        {
            if (callBack)
            {
                callBack(value);
            }
        }
    };

    rc = handler->registerRequest(
        mctpEid, instanceId, PLDM_PLATFORM, PLDM_SET_STATE_EFFECTER_STATES,
        std::move(requestMsg), std::move(setStateEffecterStatesRespHandler));
    if (rc)
    {
        lg2::error("Failed to send request to set an effecter on Host");
    }
    return rc;
}

int HostEffecterParser::setHostStateEffecter(
    size_t effecterInfoIndex, std::vector<set_effecter_state_field>& stateField,
    uint16_t effecterId)
{
    uint8_t& mctpEid = hostEffecterInfo[effecterInfoIndex].mctpEid;
    uint8_t& compEffCnt = hostEffecterInfo[effecterInfoIndex].compEffecterCnt;

    return sendSetStateEffecterStates(mctpEid, effecterId, compEffCnt,
                                      stateField);
}

void HostEffecterParser::createHostEffecterMatch(const std::string& objectPath,
                                                 const std::string& interface,
                                                 size_t effecterInfoIndex,
                                                 size_t dbusInfoIndex,
                                                 uint16_t effecterId)
{
    using namespace sdbusplus::bus::match::rules;
    effecterInfoMatch.emplace_back(
        std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            propertiesChanged(objectPath, interface),
            [this, effecterInfoIndex, dbusInfoIndex,
             effecterId](sdbusplus::message::message& msg) {
                DbusChgHostEffecterProps props;
                std::string iface;
                msg.read(iface, props);
                processHostEffecterChangeNotification(
                    props, effecterInfoIndex, dbusInfoIndex, effecterId);
            }));
}

const pldm_pdr* HostEffecterParser::getPldmPDR()
{
    return pdrRepo;
}

} // namespace host_effecters
} // namespace pldm
