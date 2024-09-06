#include "host_pdr_handler.hpp"

#include <libpldm/fru.h>
#include <libpldm/pdr.h>

#ifdef OEM_IBM
#include <libpldm/oem/ibm/fru.h>
#endif

#include "dbus/custom_dbus.hpp"
#include "dbus/deserialize.hpp"
#include "dbus/serialize.hpp"

#include <assert.h>

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/time.hpp>

#include <fstream>
#include <type_traits>

PHOSPHOR_LOG2_USING;

namespace pldm
{
using namespace pldm::responder::events;
using namespace pldm::utils;
using namespace sdbusplus::bus::match::rules;
using namespace pldm::responder::pdr_utils;
using namespace pldm::hostbmc::utils;
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace pldm::dbus;
constexpr auto fruJson = "host_frus.json";
constexpr auto ledFwdAssociation = "identifying";
constexpr auto ledReverseAssociation = "identified_by";
const Json emptyJson{};
const std::vector<Json> emptyJsonList{};

template <typename T>
uint16_t extractTerminusHandle(std::vector<uint8_t>& pdr)
{
    T* var = nullptr;
    if (std::is_same<T, pldm_pdr_fru_record_set>::value)
    {
        var = (T*)(pdr.data() + sizeof(pldm_pdr_hdr));
    }
    else
    {
        var = (T*)(pdr.data());
    }
    if (var != nullptr)
    {
        return var->terminus_handle;
    }
    return TERMINUS_HANDLE;
}

template <typename T>
void updateContainerId(pldm_entity_association_tree* entityTree,
                       std::vector<uint8_t>& pdr)
{
    T* t = nullptr;
    if (entityTree == nullptr)
    {
        return;
    }
    if (std::is_same<T, pldm_pdr_fru_record_set>::value)
    {
        t = (T*)(pdr.data() + sizeof(pldm_pdr_hdr));
    }
    else
    {
        t = (T*)(pdr.data());
    }
    if (t == nullptr)
    {
        return;
    }

    pldm_entity entity{t->entity_type, t->entity_instance, t->container_id};
    auto node = pldm_entity_association_tree_find_with_locality(entityTree,
                                                                &entity, true);
    if (node)
    {
        pldm_entity e = pldm_entity_extract(node);
        t->container_id = e.entity_container_id;
    }
}

HostPDRHandler::HostPDRHandler(
    int mctp_fd, uint8_t mctp_eid, sdeventplus::Event& event, pldm_pdr* repo,
    const std::string& eventsJsonsDir, pldm_entity_association_tree* entityTree,
    pldm_entity_association_tree* bmcEntityTree,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser,
    pldm::InstanceIdDb& instanceIdDb,
    pldm::requester::Handler<pldm::requester::Request>* handler,
    pldm::responder::oem_platform::Handler* oemPlatformHandler,
    pldm::responder::oem_utils::Handler* oemUtilsHandler,
    pldm::host_associations::HostAssociationsParser* associationsParser) :
    mctp_fd(mctp_fd), mctp_eid(mctp_eid), event(event), repo(repo),
    stateSensorHandler(eventsJsonsDir), entityTree(entityTree),
    bmcEntityTree(bmcEntityTree), hostEffecterParser(hostEffecterParser),
    instanceIdDb(instanceIdDb), handler(handler),
    associationsParser(associationsParser),
    oemPlatformHandler(oemPlatformHandler),
    entityMaps(parseEntityMap(ENTITY_MAP_JSON)),
    oemUtilsHandler(oemUtilsHandler)
{
    isHostOff = false;
    isHostRunning = false;
    mergedHostParents = false;
    hostOffMatch = std::make_unique<sdbusplus::bus::match_t>(
        pldm::utils::DBusHandler::getBus(),
        propertiesChanged("/xyz/openbmc_project/state/host0",
                          "xyz.openbmc_project.State.Host"),
        [this, repo, entityTree, bmcEntityTree](sdbusplus::message_t& msg) {
        DbusChangedProps props{};
        std::string intf;
        msg.read(intf, props);
        const auto itr = props.find("CurrentHostState");
        if (itr != props.end())
        {
            pldm::utils::PropertyValue value = itr->second;
            auto propVal = std::get<std::string>(value);
            if (propVal == "xyz.openbmc_project.State.Host.HostState.Off")
            {
                // Delete all the remote terminus information
                std::erase_if(tlPDRInfo, [](const auto& item) {
                    const auto& [key, value] = item;
                    return key != TERMINUS_HANDLE;
                });
                // when the host is powered off, set the availability
                // state of all the dbus objects to false
                this->setPresenceFrus();
                pldm_pdr_remove_remote_pdrs(repo);
                pldm_entity_association_tree_destroy_root(entityTree);
                pldm_entity_association_tree_copy_root(bmcEntityTree,
                                                       entityTree);
                this->sensorMap.clear();
                this->isHostPdrModified = false;
                this->responseReceived = false;
                this->mergedHostParents = false;
                this->stateSensorPDRs.clear();
                fruRecordSetPDRs.clear();
                isHostOff = true;
                isHostRunning = false;
                this->sensorIndex = stateSensorPDRs.begin();
                this->modifiedCounter = 0;

                // After a power off , the remote nodes will be deleted
                // from the entity association tree, making the nodes point
                // to junk values, so set them to nullptr
                for (const auto& element : this->objPathMap)
                {
                    pldm_entity obj{};
                    this->objPathMap[element.first] = obj;
                }
            }
            else if (propVal ==
                     "xyz.openbmc_project.State.Host.HostState.Running")
            {
                isHostRunning = true;
                isHostOff = false;
            }
            else
            {
                isHostRunning = false;
            }
        }
    });
}

void HostPDRHandler::setPresenceFrus()
{
    // iterate over all dbus objects
    for (const auto& [path, entityId] : objPathMap)
    {
        CustomDBus::getCustomDBus().setAvailabilityState(path, false);
    }
}

void HostPDRHandler::fetchPDR(PDRRecordHandles&& recordHandles, uint8_t tid)
{
    pdrRecordHandles.clear();
    modifiedPDRRecordHandles.clear();

    if (isHostPdrModified)
    {
        modifiedPDRRecordHandles = std::move(recordHandles);
    }
    else
    {
        pdrRecordHandles = std::move(recordHandles);
    }

    terminusID = tid;

    // Defer the actual fetch of PDRs from the host (by queuing the call on the
    // main event loop). That way, we can respond to the platform event msg from
    // the host firmware.
    pdrFetchEvent = std::make_unique<sdeventplus::source::Defer>(
        event, std::bind(std::mem_fn(&HostPDRHandler::_fetchPDR), this,
                         std::placeholders::_1));
}

void HostPDRHandler::_fetchPDR(sdeventplus::source::EventBase& /*source*/)
{
    getHostPDR();
}

void HostPDRHandler::getHostPDR(uint32_t nextRecordHandle)
{
    pdrFetchEvent.reset();

    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_PDR_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    uint32_t recordHandle{};
    if (!nextRecordHandle && (!modifiedPDRRecordHandles.empty()) &&
        isHostPdrModified)
    {
        recordHandle = modifiedPDRRecordHandles.front();
        modifiedPDRRecordHandles.pop_front();
    }
    else if (!nextRecordHandle && (!pdrRecordHandles.empty()))
    {
        recordHandle = pdrRecordHandles.front();
        pdrRecordHandles.pop_front();
    }
    else
    {
        recordHandle = nextRecordHandle;
    }
    auto instanceId = instanceIdDb.next(mctp_eid);

    auto rc = encode_get_pdr_req(instanceId, recordHandle, 0,
                                 PLDM_GET_FIRSTPART, UINT16_MAX, 0, request,
                                 PLDM_GET_PDR_REQ_BYTES);
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        error("Failed to encode get pdr request, response code '{RC}'", "RC",
              rc);
        return;
    }

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_GET_PDR,
        std::move(requestMsg),
        std::move(std::bind_front(&HostPDRHandler::processHostPDRs, this)));
    if (rc)
    {
        error(
            "Failed to send the getPDR request to remote terminus, response code '{RC}'",
            "RC", rc);
    }
}

int HostPDRHandler::handleStateSensorEvent(
    const std::vector<pldm::pdr::StateSetId>& stateSetId,
    const StateSensorEntry& entry, pdr::EventState state)
{
    for (auto& entity : objPathMap)
    {
        pldm_entity node_entity = entity.second;

        if (node_entity.entity_type != entry.entityType ||
            node_entity.entity_instance_num != entry.entityInstance ||
            node_entity.entity_container_id != entry.containerId)
        {
            continue;
        }

        for (const auto& setId : stateSetId)
        {
            if (setId == PLDM_STATE_SET_IDENTIFY_STATE)
            {
                auto ledGroupPath = updateLedGroupPath(entity.first);
                if (!ledGroupPath.empty())
                {
                    auto currVal =
                        CustomDBus::getCustomDBus().getAsserted(ledGroupPath)
                            ? "true"
                            : "false";
                    auto newVal =
                        bool(state == PLDM_STATE_SET_IDENTIFY_STATE_ASSERTED);
                    info(
                        "led state event for [ {LED_GRP_PATH} ], [ {ENTITY_TYP}, {ENTITY_NUM}, {ENTITY_ID}] , current value : [ {CURR_VAL} ] new value : [ {NEW_VAL} ]",
                        "LED_GRP_PATH", ledGroupPath, "ENTITY_TYP",
                        (unsigned)node_entity.entity_type, "ENTITY_NUM",
                        (unsigned)node_entity.entity_instance_num, "ENTITY_ID",
                        (unsigned)node_entity.entity_container_id, "CURR_VAL",
                        currVal, "NEW_VAL", (unsigned)newVal);
                    CustomDBus::getCustomDBus().setAsserted(
                        ledGroupPath, node_entity,
                        state == PLDM_STATE_SET_IDENTIFY_STATE_ASSERTED,
                        hostEffecterParser, mctp_eid);
                }
            }
        }

        if ((stateSetId[0] == PLDM_STATE_SET_HEALTH_STATE ||
             stateSetId[0] == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS))
        {
            if (!(state == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS_NORMAL) &&
                stateSetId[0] == PLDM_STATE_SET_HEALTH_STATE &&
                strstr(entity.first.c_str(), "core"))
            {
                error("Guard event on CORE : [{ENTITY_FIRST}]", "ENTITY_FIRST",
                      entity.first.c_str());
            }
            CustomDBus::getCustomDBus().setOperationalStatus(
                entity.first,
                state == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS_NORMAL,
                getParentChassis(entity.first));

            break;
        }
        else if (stateSetId[0] == PLDM_STATE_SET_VERSION)
        {
            // There is a version changed on any of the dbus objects
            info("Got a signal from Host about a possible change in Version");
            createDbusObjects();
            return PLDM_SUCCESS;
        }
    }

    auto rc = stateSensorHandler.eventAction(entry, state);
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to fetch and update D-bus property, rc = {RC}", "RC", rc);
        return rc;
    }

    return PLDM_SUCCESS;
}

void HostPDRHandler::mergeEntityAssociations(
    const std::vector<uint8_t>& pdr, [[maybe_unused]] const uint32_t& size,
    [[maybe_unused]] const uint32_t& record_handle)
{
    size_t numEntities{};
    pldm_entity* entities = nullptr;
    bool merged = false;
    auto entityPdr = reinterpret_cast<pldm_pdr_entity_association*>(
        const_cast<uint8_t*>(pdr.data()) + sizeof(pldm_pdr_hdr));

    if (oemPlatformHandler &&
        oemPlatformHandler->checkRecordHandleInRange(record_handle))
    {
        // Adding the remote range PDRs to the repo before merging it
        uint32_t handle = record_handle;
        pldm_pdr_add_check(repo, pdr.data(), size, true, 0xFFFF, &handle);
    }

    pldm_entity_association_pdr_extract(pdr.data(), pdr.size(), &numEntities,
                                        &entities);
    if (numEntities > 0)
    {
        pldm_entity_node* pNode = nullptr;
        if (!mergedHostParents)
        {
            pNode = pldm_entity_association_tree_find_with_locality(
                entityTree, &entities[0], false);
        }
        else
        {
            pNode = pldm_entity_association_tree_find_with_locality(
                entityTree, &entities[0], true);
        }
        if (!pNode)
        {
            return;
        }

        Entities entityAssoc;
        entityAssoc.push_back(pNode);
        for (size_t i = 1; i < numEntities; ++i)
        {
            auto node = pldm_entity_association_tree_add_entity(
                entityTree, &entities[i], entities[i].entity_instance_num,
                pNode, entityPdr->association_type, true,
                !(entities[i].entity_container_id & 0x8000), 0xFFFF);
            if (!node)
            {
                continue;
            }
            merged = true;
            entityAssoc.push_back(node);
        }

        mergedHostParents = true;
        if (merged)
        {
            entityAssociations.push_back(entityAssoc);
        }
    }

    if (merged)
    {
        // Update our PDR repo with the merged entity association PDRs
        pldm_entity_node* node = nullptr;
        pldm_find_entity_ref_in_tree(entityTree, entities[0], &node);
        if (node == nullptr)
        {
            error("Failed to find referrence of the entity in the tree");
        }

        else
        {
            uint16_t terminus_handle = 0;
            for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
            {
                if (std::get<0>(terminusInfo) == terminusID &&
                    std::get<1>(terminusInfo) == mctp_eid &&
                    std::get<2>(terminusInfo))
                {
                    terminus_handle = terminusHandle;
                }
            }

            if ((isHostUp() || (terminus_handle & 0x8000)) &&
                oemPlatformHandler)
            {
                auto record = oemPlatformHandler->fetchLastBMCRecord(repo);

                uint32_t record_handle = pldm_pdr_get_record_handle(repo,
                                                                    record);
                int rc =
                    pldm_entity_association_pdr_add_from_node_with_record_handle(
                        node, repo, &entities, numEntities, true,
                        TERMINUS_HANDLE, (record_handle + 1));
                if (rc)
                {
                    error("Failed to add entity association PDR from node:{RC}",
                          "RC", rc);
                }
            }
            else
            {
                auto record = oemPlatformHandler->fetchLastBMCRecord(repo);

                uint32_t record_handle = pldm_pdr_get_record_handle(repo,
                                                                    record);
                int rc =
                    pldm_entity_association_pdr_add_from_node_with_record_handle(
                        node, repo, &entities, numEntities, true,
                        terminus_handle, (record_handle + 1));
                if (rc)
                {
                    error("Failed to add entity association PDR from node:{RC}",
                          "RC", rc);
                }
            }
        }
    }
    free(entities);
}

void HostPDRHandler::sendPDRRepositoryChgEvent(std::vector<uint8_t>&& pdrTypes,
                                               uint8_t eventDataFormat)
{
    info(
        "Sending the repo change event after merging the PDRs, MCTP_ID: {MCTP_ID}",
        "MCTP_ID", mctp_eid);
    assert(eventDataFormat == FORMAT_IS_PDR_HANDLES);

    // Extract from the PDR repo record handles of PDRs we want the host
    // to pull up.
    std::vector<uint8_t> eventDataOps{PLDM_RECORDS_ADDED};
    std::vector<uint8_t> numsOfChangeEntries(1);
    std::vector<std::vector<ChangeEntry>> changeEntries(
        numsOfChangeEntries.size());
    for (auto pdrType : pdrTypes)
    {
        const pldm_pdr_record* record{};
        do
        {
            record = pldm_pdr_find_record_by_type(repo, pdrType, record,
                                                  nullptr, nullptr);
            if (record && pldm_pdr_record_is_remote(record))
            {
                uint16_t th = pldm_pdr_get_terminus_handle(repo, record);
                if (!isHostUp())
                {
                    for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
                    {
                        if (std::get<0>(terminusInfo) == terminusID &&
                            std::get<1>(terminusInfo) == mctp_eid &&
                            std::get<2>(terminusInfo) && th == terminusHandle)
                        {
                            // send record handles of that terminus only.
                            changeEntries[0].push_back(
                                pldm_pdr_get_record_handle(repo, record));
                        }
                    }
                }
                else
                {
                    changeEntries[0].push_back(
                        pldm_pdr_get_record_handle(repo, record));
                }
            }
        } while (record);
    }
    if (changeEntries.empty())
    {
        return;
    }
    numsOfChangeEntries[0] = changeEntries[0].size();

    // Encode PLDM platform event msg to indicate a PDR repo change.
    size_t maxSize = PLDM_PDR_REPOSITORY_CHG_EVENT_MIN_LENGTH +
                     PLDM_PDR_REPOSITORY_CHANGE_RECORD_MIN_LENGTH +
                     changeEntries[0].size() * sizeof(uint32_t);
    std::vector<uint8_t> eventDataVec{};
    eventDataVec.resize(maxSize);
    auto eventData =
        reinterpret_cast<struct pldm_pdr_repository_chg_event_data*>(
            eventDataVec.data());
    size_t actualSize{};
    auto firstEntry = changeEntries[0].data();
    auto rc = encode_pldm_pdr_repository_chg_event_data(
        eventDataFormat, 1, eventDataOps.data(), numsOfChangeEntries.data(),
        &firstEntry, eventData, &actualSize, maxSize);
    if (rc != PLDM_SUCCESS)
    {
        error(
            "Failed to encode pldm pdr repository change event data, response code '{RC}'",
            "RC", rc);
        return;
    }
    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES +
                                    actualSize);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    rc = encode_platform_event_message_req(
        instanceId, 1, TERMINUS_ID, PLDM_PDR_REPOSITORY_CHG_EVENT,
        eventDataVec.data(), actualSize, request,
        actualSize + PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES);
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        error(
            "Failed to encode platform event message request, response code '{RC}'",
            "RC", rc);
        return;
    }

    auto platformEventMessageResponseHandler =
        [](mctp_eid_t /*eid*/, const pldm_msg* response, size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error(
                "Failed to receive response for the PDR repository changed event");
            return;
        }

        uint8_t completionCode{};
        uint8_t status{};
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_platform_event_message_resp(responsePtr, respMsgLen,
                                                     &completionCode, &status);
        if (rc || completionCode)
        {
            error(
                "Failed to decode platform event message response, response code '{RC}' and completion code '{CC}'",
                "RC", rc, "CC", completionCode);
        }
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_PLATFORM_EVENT_MESSAGE,
        std::move(requestMsg), std::move(platformEventMessageResponseHandler));
    if (rc)
    {
        error(
            "Failed to send the PDR repository changed event request, response code '{RC}'",
            "RC", rc);
    }
}

void HostPDRHandler::parseStateSensorPDRs()
{
    for (const auto& pdr : stateSensorPDRs)
    {
        SensorEntry sensorEntry{};
        const auto& [terminusHandle, sensorID, sensorInfo] =
            responder::pdr_utils::parseStateSensorPDR(pdr);
        sensorEntry.sensorID = sensorID;
        try
        {
            sensorEntry.terminusID = std::get<0>(tlPDRInfo.at(terminusHandle));
        }
        // If there is no mapping for terminusHandle assign the reserved TID
        // value of 0xFF to indicate that.
        catch (const std::out_of_range&)
        {
            error("Terminus handle out of range {HAN}", "HAN", terminusHandle);
            sensorEntry.terminusID = PLDM_TID_RESERVED;
        }
        sensorMap.emplace(sensorEntry, std::move(sensorInfo));
    }
}

void HostPDRHandler::processHostPDRs(mctp_eid_t /*eid*/,
                                     const pldm_msg* response,
                                     size_t respMsgLen)
{
    static bool merged = false;
    uint32_t nextRecordHandle{};
    uint8_t tlEid = 0;
    bool tlValid = true;
    uint32_t rh = 0;
    uint16_t terminusHandle = 0;
    uint16_t pdrTerminusHandle = 0;
    uint8_t tid = 0;

    uint8_t completionCode{};
    uint32_t nextDataTransferHandle{};
    uint8_t transferFlag{};
    uint16_t respCount{};
    uint8_t transferCRC{};
    if (response == nullptr || !respMsgLen)
    {
        error("Failed to receive response for the GetPDR command");
        if (isHostRunning)
        {
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.GetPDR.PDRExchangeFailure");
        }
        return;
    }

    auto rc = decode_get_pdr_resp(
        response, respMsgLen /*- sizeof(pldm_msg_hdr)*/, &completionCode,
        &nextRecordHandle, &nextDataTransferHandle, &transferFlag, &respCount,
        nullptr, 0, &transferCRC);
    std::vector<uint8_t> responsePDRMsg;
    responsePDRMsg.resize(respMsgLen + sizeof(pldm_msg_hdr));
    memcpy(responsePDRMsg.data(), response, respMsgLen + sizeof(pldm_msg_hdr));
    if (rc != PLDM_SUCCESS)
    {
        error(
            "Failed to decode getPDR response for next record handle '{NEXT_RECORD_HANDLE}', response code '{RC}'",
            "NEXT_RECORD_HANDLE", nextRecordHandle, "RC", rc);
        return;
    }
    else
    {
        std::vector<uint8_t> pdr(respCount, 0);
        rc = decode_get_pdr_resp(response, respMsgLen, &completionCode,
                                 &nextRecordHandle, &nextDataTransferHandle,
                                 &transferFlag, &respCount, pdr.data(),
                                 respCount, &transferCRC);
        if (rc != PLDM_SUCCESS || completionCode != PLDM_SUCCESS)
        {
            error(
                "Failed to decode getPDR response for next record handle '{NEXT_RECORD_HANDLE}', next data transfer handle '{DATA_TRANSFER_HANDLE}' and transfer flag '{FLAG}', response code '{RC}' and completion code '{CC}'",
                "NEXT_RECORD_HANDLE", nextRecordHandle, "DATA_TRANSFER_HANDLE",
                nextDataTransferHandle, "FLAG", transferFlag, "RC", rc, "CC",
                completionCode);
            return;
        }
        else
        {
            // when nextRecordHandle is 0, we need the recordHandle of the last
            // PDR and not 0-1.
            if (!nextRecordHandle)
            {
                rh = nextRecordHandle;
            }
            else
            {
                rh = nextRecordHandle - 1;
            }

            auto pdrHdr = reinterpret_cast<pldm_pdr_hdr*>(pdr.data());
            if (!rh)
            {
                rh = pdrHdr->record_handle;
            }

            if (pdrHdr->type == PLDM_PDR_ENTITY_ASSOCIATION)
            {
                this->mergeEntityAssociations(pdr, respCount, rh);
                merged = true;
            }
            else
            {
                if (pdrHdr->type == PLDM_TERMINUS_LOCATOR_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_terminus_locator_pdr>(pdr);
                    auto tlpdr =
                        reinterpret_cast<const pldm_terminus_locator_pdr*>(
                            pdr.data());

                    terminusHandle = tlpdr->terminus_handle;
                    tid = tlpdr->tid;
                    auto terminus_locator_type = tlpdr->terminus_locator_type;
                    if (terminus_locator_type ==
                        PLDM_TERMINUS_LOCATOR_TYPE_MCTP_EID)
                    {
                        auto locatorValue = reinterpret_cast<
                            const pldm_terminus_locator_type_mctp_eid*>(
                            tlpdr->terminus_locator_value);
                        tlEid = static_cast<uint8_t>(locatorValue->eid);
                    }
                    if (tlpdr->validity == 0)
                    {
                        info("Got a TL PDR with valid bit false");
                        tlValid = false;
                    }

                    tlPDRInfo.insert_or_assign(
                        tlpdr->terminus_handle,
                        std::make_tuple(tlpdr->tid, tlEid, tlpdr->validity));
                }
                else if (pdrHdr->type == PLDM_STATE_SENSOR_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_state_sensor_pdr>(pdr);
                    updateContainerId<pldm_state_sensor_pdr>(entityTree, pdr);
                    stateSensorPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_PDR_FRU_RECORD_SET)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_pdr_fru_record_set>(pdr);
                    updateContainerId<pldm_pdr_fru_record_set>(entityTree, pdr);
                    fruRecordSetPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_STATE_EFFECTER_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_state_effecter_pdr>(pdr);
                    updateContainerId<pldm_state_effecter_pdr>(entityTree, pdr);
                }
                else if (pdrHdr->type == PLDM_NUMERIC_EFFECTER_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_numeric_effecter_value_pdr>(
                            pdr);
                    updateContainerId<pldm_numeric_effecter_value_pdr>(
                        entityTree, pdr);
                }
                // if the TLPDR is invalid update the repo accordingly
                if (!tlValid)
                {
                    info(
                        "Got a invalid TL PDR need a update for tid: '{TID}' and EID = {EID}",
                        "TID", tid, "EID", tlEid);
                    pldm_pdr_update_TL_pdr(repo, terminusHandle, tid, tlEid,
                                           tlValid);

                    if (!isHostUp())
                    {
                        // The terminus PDR becomes invalid when the terminus
                        // itself is down. We don't need to do PDR exchange in
                        // that case, so setting the next record handle to 0.
                        nextRecordHandle = 0;
                    }
                }
                else
                {
                    if ((isHostPdrModified == true) || !(modifiedCounter == 0))
                    {
                        pldm_delete_by_record_handle(repo, rh, true);

                        rc = pldm_pdr_add_check(repo, pdr.data(), respCount,
                                                true, pdrTerminusHandle, &rh);
                        if (rc)
                        {
                            throw std::runtime_error(
                                "Failed to add PDR when isHostPdrModified is true");
                        }

                        if ((pdrHdr->type == PLDM_STATE_EFFECTER_PDR) &&
                            (oemPlatformHandler))
                        {
                            auto effecterPdr = reinterpret_cast<
                                const pldm_state_effecter_pdr*>(pdr.data());
                            auto entityType = effecterPdr->entity_type;
                            auto statesPtr = effecterPdr->possible_states;
                            auto compEffCount =
                                effecterPdr->composite_effecter_count;

                            while (compEffCount--)
                            {
                                auto state = reinterpret_cast<
                                    const state_effecter_possible_states*>(
                                    statesPtr);
                                auto stateSetID = state->state_set_id;
                                oemPlatformHandler->modifyPDROemActions(
                                    entityType, stateSetID);

                                if (compEffCount)
                                {
                                    statesPtr +=
                                        sizeof(state_effecter_possible_states) +
                                        state->possible_states_size - 1;
                                }
                            }
                        }
                        modifiedCounter--;
                    }
                    // We need to look for an optimal solution for this, we are
                    // unexpectedly entering this path when we receive multiple
                    // modified PDR repo change events
                    else if ((isHostPdrModified != true) &&
                             (modifiedCounter == 0))
                    {
                        pldm_delete_by_record_handle(repo, rh, true);

                        rc = pldm_pdr_add_check(repo, pdr.data(), respCount,
                                                true, pdrTerminusHandle, &rh);
                        if (rc)
                        {
                            throw std::runtime_error(
                                "Failed to add PDR when isHostPdrModified is not true");
                        }
                    }
                    else
                    {
                        rc = pldm_pdr_add_check(repo, pdr.data(), respCount,
                                                true, pdrTerminusHandle, &rh);
                        if (rc)
                        {
                            throw std::runtime_error("Failed to add PDR");
                        }
                    }
                }
            }
        }
    }
    if (!nextRecordHandle)
    {
        auto firstRecord = pldm_pdr_get_record_handle(
            repo, pldm_pdr_find_last_in_range(repo, 1, 1));
        auto lastRecord = pldm_pdr_get_record_handle(
            repo, pldm_pdr_find_last_in_range(repo, 1, 0x02FFFFFF));
        info("First Record in the repo after PDR exchange is: {FIRST_REC_HNDL}",
             "FIRST_REC_HNDL", firstRecord);
        info("Last Record in the repo after PDR exchange is: {LAST_REC_HNDL}",
             "LAST_REC_HNDL", lastRecord);

        for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
        {
            info(
                "TerminusHandle:'{TERMINUS_HANDLE}', TID:'{TID}', EID:'{EID}', Validity:{VALID}",
                "TERMINUS_HANDLE", terminusHandle, "TID",
                std::get<0>(terminusInfo), "EID", std::get<1>(terminusInfo),
                "VALID", std::get<2>(terminusInfo));
        }

        updateEntityAssociation(entityAssociations, entityTree, objPathMap,
                                entityMaps, oemPlatformHandler);
        pldm::serialize::Serialize::getSerialize().setObjectPathMaps(
            objPathMap);
        if (oemUtilsHandler)
        {
            oemUtilsHandler->setCoreCount(entityAssociations, entityMaps);
        }
        /*received last record*/
        this->parseStateSensorPDRs();
        this->createDbusObjects();
        if (isHostUp())
        {
            info("Host is UP & Completed the PDR Exchange with host");
            this->setHostSensorState();
        }
        entityAssociations.clear();

        mergedHostParents = false;

        if (merged)
        {
            merged = false;
            deferredPDRRepoChgEvent =
                std::make_unique<sdeventplus::source::Defer>(
                    event,
                    std::bind(
                        std::mem_fn((&HostPDRHandler::_processPDRRepoChgEvent)),
                        this, std::placeholders::_1));
        }
    }
    else
    {
        if (modifiedPDRRecordHandles.empty() && isHostPdrModified)
        {
            isHostPdrModified = false;
        }
        else
        {
            deferredFetchPDREvent =
                std::make_unique<sdeventplus::source::Defer>(
                    event,
                    std::bind(
                        std::mem_fn((&HostPDRHandler::_processFetchPDREvent)),
                        this, nextRecordHandle, std::placeholders::_1));
        }
    }
}

void HostPDRHandler::_processPDRRepoChgEvent(
    sdeventplus::source::EventBase& /*source */)
{
    deferredPDRRepoChgEvent.reset();
    this->sendPDRRepositoryChgEvent(
        std::move(std::vector<uint8_t>(1, PLDM_PDR_ENTITY_ASSOCIATION)),
        FORMAT_IS_PDR_HANDLES);
}

void HostPDRHandler::_processFetchPDREvent(
    uint32_t nextRecordHandle, sdeventplus::source::EventBase& /*source */)
{
    deferredFetchPDREvent.reset();
    if (!this->pdrRecordHandles.empty())
    {
        nextRecordHandle = this->pdrRecordHandles.front();
        this->pdrRecordHandles.pop_front();
    }
    else if (isHostPdrModified && (!this->modifiedPDRRecordHandles.empty()))
    {
        nextRecordHandle = this->modifiedPDRRecordHandles.front();
        this->modifiedPDRRecordHandles.pop_front();
    }
    this->getHostPDR(nextRecordHandle);
}

void HostPDRHandler::setHostFirmwareCondition()
{
    responseReceived = false;
    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_VERSION_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_version_req(instanceId, 0, PLDM_GET_FIRSTPART,
                                     PLDM_BASE, request);
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to encode GetPLDMVersion, response code {RC}", "RC",
              lg2::hex, rc);
        instanceIdDb.free(mctp_eid, instanceId);
        return;
    }

    auto getPLDMVersionHandler = [this](mctp_eid_t /*eid*/,
                                        const pldm_msg* response,
                                        size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error(
                "Failed to receive response for getPLDMVersion command, Host seems to be off");
            return;
        }
        info("Getting the response code '{RC}'", "RC", lg2::hex,
             static_cast<uint16_t>(response->payload[0]));
        this->responseReceived = true;
    };
    rc = handler->registerRequest(mctp_eid, instanceId, PLDM_BASE,
                                  PLDM_GET_PLDM_VERSION, std::move(requestMsg),
                                  std::move(getPLDMVersionHandler));
    if (rc)
    {
        error(
            "Failed to discover remote terminus state. Assuming remote terminus as off");
    }
}

bool HostPDRHandler::isHostUp()
{
    return responseReceived;
}

void HostPDRHandler::setHostSensorState()
{
    sensorIndex = stateSensorPDRs.begin();
    _setHostSensorState();
}

void HostPDRHandler::_setHostSensorState()
{
    if (isHostOff)
    {
        error(
            "set host state sensor begin : Host is off, stopped sending sensor state commands");
        return;
    }
    if (sensorIndex != stateSensorPDRs.end())
    {
        uint8_t mctpEid = pldm::utils::readHostEID();
        std::vector<uint8_t> stateSensorPDR = *sensorIndex;
        auto pdr = reinterpret_cast<const pldm_state_sensor_pdr*>(
            stateSensorPDR.data());

        if (!pdr)
        {
            error("Failed to get State sensor PDR");
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.SetHostSensorState.GetStateSensorPDRFail");
            return;
        }
        uint16_t sensorId = pdr->sensor_id;

        for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
        {
            if (terminusHandle == pdr->terminus_handle)
            {
                if (std::get<2>(terminusInfo) == PLDM_TL_PDR_VALID)
                {
                    mctpEid = std::get<1>(terminusInfo);
                }

                bitfield8_t sensorRearm;
                sensorRearm.byte = 0;
                uint8_t tid = std::get<0>(terminusInfo);

                auto instanceId = instanceIdDb.next(mctpEid);
                std::vector<uint8_t> requestMsg(
                    sizeof(pldm_msg_hdr) +
                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);
                auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
                auto rc = encode_get_state_sensor_readings_req(
                    instanceId, sensorId, sensorRearm, 0, request);

                if (rc != PLDM_SUCCESS)
                {
                    instanceIdDb.free(mctpEid, instanceId);
                    error(
                        "Failed to encode get state sensor readings request for sensorID '{SENSOR_ID}' and  instanceID '{INSTANCE}', response code '{RC}'",
                        "SENSOR_ID", sensorId, "INSTANCE", instanceId, "RC",
                        rc);
                    pldm::utils::reportError(
                        "xyz.openbmc_project.PLDM.Error.SetHostSensorState.EncodeStateSensorFail");
                    return;
                }
                auto getStateSensorReadingRespHandler =
                    [=, this](mctp_eid_t /*eid*/, const pldm_msg* response,
                              size_t respMsgLen) {
                    if (response == nullptr || !respMsgLen)
                    {
                        error(
                            "Failed to receive response for getStateSensorReading command for sensor id = {SENSOR_ID}",
                            "SENSOR_ID", sensorId);
                        if (this->isHostOff)
                        {
                            error(
                                "set host state sensor : Host is off, stopped sending sensor state commands");
                            return;
                        }
                        if (sensorIndex == stateSensorPDRs.end())
                        {
                            return;
                        }
                        ++sensorIndex;
                        _setHostSensorState();
                        return;
                    }
                    std::array<get_sensor_state_field, 8> stateField{};
                    uint8_t completionCode = 0;
                    uint8_t comp_sensor_count = 0;

                    auto rc = decode_get_state_sensor_readings_resp(
                        response, respMsgLen, &completionCode,
                        &comp_sensor_count, stateField.data());

                    if (rc != PLDM_SUCCESS || completionCode != PLDM_SUCCESS)
                    {
                        error(
                            "Failed to decode get state sensor readings response for sensorID '{SENSOR_ID}' and  instanceID '{INSTANCE}', response code'{RC}' and completion code '{CC}'",
                            "SENSOR_ID", sensorId, "INSTANCE", instanceId, "RC",
                            rc, "CC", completionCode);
                        if (sensorIndex == stateSensorPDRs.end())
                        {
                            return;
                        }
                        ++sensorIndex;
                        _setHostSensorState();
                        return;
                    }

                    uint8_t eventState;
                    uint8_t previousEventState;
                    uint8_t sensorOffset = comp_sensor_count - 1;

                    for (size_t i = 0; i < comp_sensor_count; i++)
                    {
                        eventState = stateField[i].present_state;
                        previousEventState = stateField[i].previous_state;

                        emitStateSensorEventSignal(tid, sensorId, sensorOffset,
                                                   eventState,
                                                   previousEventState);

                        SensorEntry sensorEntry{tid, sensorId};

                        pldm::pdr::EntityInfo entityInfo{};
                        pldm::pdr::CompositeSensorStates
                            compositeSensorStates{};
                        std::vector<pldm::pdr::StateSetId> stateSetIds{};

                        try
                        {
                            std::tie(entityInfo, compositeSensorStates,
                                     stateSetIds) =
                                lookupSensorInfo(sensorEntry);
                        }
                        catch (const std::out_of_range&)
                        {
                            try
                            {
                                sensorEntry.terminusID = PLDM_TID_RESERVED;
                                std::tie(entityInfo, compositeSensorStates,
                                         stateSetIds) =
                                    lookupSensorInfo(sensorEntry);
                            }
                            catch (const std::out_of_range&)
                            {
                                error("No mapping for the events");
                                continue;
                            }
                        }

                        if (sensorOffset > compositeSensorStates.size())
                        {
                            error(
                                "Error Invalid data, Invalid sensor offset, SensorId = {SENSOR_ID}",
                                "SENSOR_ID", sensorId);
                            return;
                        }

                        const auto& possibleStates =
                            compositeSensorStates[sensorOffset];
                        if (possibleStates.find(eventState) ==
                            possibleStates.end())
                        {
                            error(
                                "Error invalid_data, Invalid event state, SensorId = {SENSOR_ID}",
                                "SENSOR_ID", sensorId);
                            return;
                        }
                        const auto& [containerId, entityType,
                                     entityInstance] = entityInfo;
                        auto stateSetId = stateSetIds[sensorOffset];
                        pldm::responder::events::StateSensorEntry
                            stateSensorEntry{containerId,    entityType,
                                             entityInstance, sensorOffset,
                                             stateSetId,     false};
                        handleStateSensorEvent(stateSetIds, stateSensorEntry,
                                               eventState);
                    }

                    if (sensorIndex == stateSensorPDRs.end())
                    {
                        return;
                    }
                    ++sensorIndex;
                    _setHostSensorState();
                };

                rc = handler->registerRequest(
                    mctpEid, instanceId, PLDM_PLATFORM,
                    PLDM_GET_STATE_SENSOR_READINGS, std::move(requestMsg),
                    std::move(getStateSensorReadingRespHandler));

                if (rc != PLDM_SUCCESS)
                {
                    error(
                        " Failed to send request to get State sensor reading on Host, SensorId={SENSOR_ID}",
                        "SENSOR_ID", sensorId);
                }
            }
        }
    }
}

void HostPDRHandler::getFRURecordTableMetadataByRemote()
{
    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(
        sizeof(pldm_msg_hdr) + PLDM_GET_FRU_RECORD_TABLE_METADATA_REQ_BYTES);

    // GetFruRecordTableMetadata
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_fru_record_table_metadata_req(
        instanceId, request, requestMsg.size() - sizeof(pldm_msg_hdr));
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        error(
            "Failed to encode get fru record table metadata request, response code '{RC}'",
            "RC", lg2::hex, rc);
        return;
    }

    auto getFruRecordTableMetadataResponseHandler =
        [this](mctp_eid_t /*eid*/, const pldm_msg* response,
               size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error(
                "Failed to receive response for the get fru record table metadata");
            return;
        }

        uint8_t cc = 0;
        uint8_t fru_data_major_version, fru_data_minor_version;
        uint32_t fru_table_maximum_size, fru_table_length;
        uint16_t total_record_set_identifiers;
        uint16_t total;
        uint32_t checksum;

        auto rc = decode_get_fru_record_table_metadata_resp(
            response, respMsgLen, &cc, &fru_data_major_version,
            &fru_data_minor_version, &fru_table_maximum_size, &fru_table_length,
            &total_record_set_identifiers, &total, &checksum);

        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            error(
                "Failed to decode get fru record table metadata response, response code '{RC}' and completion code '{CC}'",
                "RC", lg2::hex, rc, "CC", cc);
            return;
        }
        info("Fru RecordTable length {LEN}", "LEN", total);
        // pass total to getFRURecordTableByRemote
        this->getFRURecordTableByRemote(total);
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_FRU, PLDM_GET_FRU_RECORD_TABLE_METADATA,
        std::move(requestMsg),
        std::move(getFruRecordTableMetadataResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        error(
            "Failed to send the the set state effecter states request, response code '{RC}'",
            "RC", rc);
    }

    return;
}

void HostPDRHandler::getFRURecordTableByRemote(uint16_t& totalTableRecords)
{
    fruRecordData.clear();

    if (!totalTableRecords)
    {
        error("Failed to get fru record table");
        return;
    }

    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_FRU_RECORD_TABLE_REQ_BYTES);

    // send the getFruRecordTable command
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_fru_record_table_req(
        instanceId, 0, PLDM_GET_FIRSTPART, request,
        requestMsg.size() - sizeof(pldm_msg_hdr));
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        error(
            "Failed to encode get fru record table request, response code '{RC}'",
            "RC", lg2::hex, rc);
        return;
    }

    auto getFruRecordTableResponseHandler =
        [totalTableRecords, this](mctp_eid_t /*eid*/, const pldm_msg* response,
                                  size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error("Failed to receive response for the get fru record table");
            return;
        }

        uint8_t cc = 0;
        uint32_t next_data_transfer_handle = 0;
        uint8_t transfer_flag = 0;
        size_t fru_record_table_length = 0;
        std::vector<uint8_t> fru_record_table_data(respMsgLen);
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_get_fru_record_table_resp(
            responsePtr, respMsgLen, &cc, &next_data_transfer_handle,
            &transfer_flag, fru_record_table_data.data(),
            &fru_record_table_length);

        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            error(
                "Failed to decode get fru record table resp, response code '{RC}' and completion code '{CC}'",
                "RC", lg2::hex, rc, "CC", cc);
            return;
        }

        fruRecordData = responder::pdr_utils::parseFruRecordTable(
            fru_record_table_data.data(), fru_record_table_length);

        if (totalTableRecords != fruRecordData.size())
        {
            fruRecordData.clear();

            error("Failed to parse fru recrod data format.");
            return;
        }

        this->setFRUDataOnDBus(fruRecordData);
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_FRU, PLDM_GET_FRU_RECORD_TABLE,
        std::move(requestMsg), std::move(getFruRecordTableResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to send the the set state effecter states request");
    }
}

pdr::EID HostPDRHandler::getMctpEID(const pldm::pdr::TerminusID& tid)
{
    for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
    {
        if (std::get<0>(terminusInfo) == tid)
        {
            return std::get<1>(terminusInfo);
        }
    }
    return pldm::utils::readHostEID();
}

void HostPDRHandler::getPresentStateBySensorReadigs(
    const pldm::pdr::TerminusID& tid, uint16_t sensorId, uint16_t type,
    uint16_t instance, uint16_t containerId, const std::string& path,
    pldm::pdr::StateSetId stateSetId)
{
    auto mctpEid = getMctpEID(tid);
    auto instanceId = instanceIdDb.next(mctpEid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);

    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    bitfield8_t bf;
    bf.byte = 0;
    auto rc = encode_get_state_sensor_readings_req(instanceId, sensorId, bf, 0,
                                                   request);
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctpEid, instanceId);
        error("Failed to encode_get_state_sensor_readings_req, rc = {RC}", "RC",
              rc);
        return;
    }

    auto getStateSensorReadingsResponseHandler =
        [this, path, type, instance, containerId, stateSetId, mctpEid,
         sensorId](mctp_eid_t /*eid*/, const pldm_msg* response,
                   size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error(
                "Failed to receive response for get_state_sensor_readings command, sensor id : {SENSOR_ID}",
                "SENSOR_ID", sensorId);
            // even when for some reason , if we fail to get a response
            // to one sensor, try all the dbus objects

            if (this->isHostOff)
            {
                error("Host is off, stopped sending sensor states command");
                return;
            }

            ++sensorMapIndex;
            if (sensorMapIndex == sensorMap.end())
            {
                ++objMapIndex;
                sensorMapIndex = sensorMap.begin();
            }
            setOperationStatus();
            return;
        }

        uint8_t cc = 0;
        uint8_t sensorCnt = 0;
        uint8_t state = PLDM_SENSOR_UNKNOWN;
        std::array<get_sensor_state_field, 8> stateField{};
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_get_state_sensor_readings_resp(
            responsePtr, respMsgLen, &cc, &sensorCnt, stateField.data());
        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            error(
                "Failed to decode get state sensor readings resp, Message Error: rc = {RC}, cc = {CC}",
                "RC", rc, "CC", (int)cc);
            ++sensorMapIndex;
            if (sensorMapIndex == sensorMap.end())
            {
                ++objMapIndex;
                sensorMapIndex = sensorMap.begin();
            }

            setOperationStatus();
            return;
        }

        for (const auto& filed : stateField)
        {
            state = filed.present_state;
            break;
        }

        if (stateSetId == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS ||
            stateSetId == PLDM_STATE_SET_HEALTH_STATE)
        {
            // set the dbus property only when its not a composite sensor
            // and the state set it PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS
            // Get sensorOpState property by the getStateSensorReadings
            // command.

            CustomDBus::getCustomDBus().setOperationalStatus(
                path, state == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS_NORMAL,
                getParentChassis(path));
        }
        else if (stateSetId == PLDM_STATE_SET_IDENTIFY_STATE)
        {
            auto ledGroupPath = updateLedGroupPath(path);
            if (!ledGroupPath.empty())
            {
                pldm_entity entity{type, instance, containerId};
                CustomDBus::getCustomDBus().setAsserted(
                    ledGroupPath, entity,
                    state == PLDM_STATE_SET_IDENTIFY_STATE_ASSERTED,
                    hostEffecterParser, mctpEid);
                std::vector<std::tuple<std::string, std::string, std::string>>
                    associations{{ledFwdAssociation, ledReverseAssociation,
                                  ledGroupPath}};
                CustomDBus::getCustomDBus().setAssociations(path, associations);
            }
        }

        ++sensorMapIndex;
        if (sensorMapIndex == sensorMap.end())
        {
            ++objMapIndex;
            sensorMapIndex = sensorMap.begin();
        }
        setOperationStatus();
    };

    rc = handler->registerRequest(
        mctpEid, instanceId, PLDM_PLATFORM, PLDM_GET_STATE_SENSOR_READINGS,
        std::move(requestMsg),
        std::move(getStateSensorReadingsResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to get the State Sensor Readings request");
    }

    return;
}

std::optional<uint16_t> HostPDRHandler::getRSI(const pldm_entity& entity)
{
    for (const auto& pdr : fruRecordSetPDRs)
    {
        auto fruPdr = reinterpret_cast<const pldm_pdr_fru_record_set*>(
            const_cast<uint8_t*>(pdr.data()) + sizeof(pldm_pdr_hdr));

        if (fruPdr->entity_type == entity.entity_type &&
            fruPdr->entity_instance == entity.entity_instance_num &&
            fruPdr->container_id == entity.entity_container_id)
        {
            return fruPdr->fru_rsi;
        }
    }

    return std::nullopt;
}

void HostPDRHandler::setFRUDataOnDBus(
    [[maybe_unused]] const std::vector<
        responder::pdr_utils::FruRecordDataFormat>& fruRecordData)
{
    for (const auto& entity : objPathMap)
    {
        auto fruRSI = getRSI(entity.second);

        for (const auto& data : fruRecordData)
        {
            if (!fruRSI.has_value() || (*fruRSI != data.fruRSI))
            {
                continue;
            }

#ifdef OEM_IBM
            if (data.fruRecType == PLDM_FRU_RECORD_TYPE_OEM)
            {
                for (const auto& tlv : data.fruTLV)
                {
                    if (tlv.fruFieldType ==
                        PLDM_OEM_FRU_FIELD_TYPE_LOCATION_CODE)
                    {
                        CustomDBus::getCustomDBus().setLocationCode(
                            entity.first,
                            std::string(reinterpret_cast<const char*>(
                                            tlv.fruFieldValue.data()),
                                        tlv.fruFieldLen));
                    }
                }
            }
            else
#endif
            {
                for (auto& tlv : data.fruTLV)
                {
                    if (tlv.fruFieldType == PLDM_FRU_FIELD_TYPE_VERSION &&
                        entity.second.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
                    {
                        auto mex_vers =
                            std::string(reinterpret_cast<const char*>(
                                            tlv.fruFieldValue.data()),
                                        tlv.fruFieldLen);
                        info("Refreshing the mex firmware version : {MEX_VERS}",
                             "MEX_VERS", mex_vers);
                        CustomDBus::getCustomDBus().setSoftwareVersion(
                            entity.first,
                            std::string(reinterpret_cast<const char*>(
                                            tlv.fruFieldValue.data()),
                                        tlv.fruFieldLen));
                    }
                }
            }
        }
    }
}

bool HostPDRHandler::getValidity(const pldm::pdr::TerminusID& tid)
{
    for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
    {
        if (std::get<0>(terminusInfo) == tid)
        {
            if (std::get<2>(terminusInfo) == PLDM_TL_PDR_NOT_VALID)
            {
                return false;
            }
            return true;
        }
    }
    return false;
}

void HostPDRHandler::setPresentPropertyStatus(const std::string& path)
{
    CustomDBus::getCustomDBus().updateItemPresentStatus(path, true);
}

void HostPDRHandler::setAvailabilityState(const std::string& path)
{
    CustomDBus::getCustomDBus().setAvailabilityState(path, true);
}

void HostPDRHandler::createDbusObjects()
{
    error("Refreshing dbus hosted by pldm Started");

    objMapIndex = objPathMap.begin();

    sensorMapIndex = sensorMap.begin();

    for (const auto& entity : objPathMap)
    {
        // update the Present Property
        setPresentPropertyStatus(entity.first);

        // Implement & update the Availability to true
        setAvailabilityState(entity.first);

        switch (entity.second.entity_type)
        {
            case PLDM_ENTITY_PROC | 0x8000:
                CustomDBus::getCustomDBus().implementCpuCoreInterface(
                    entity.first);
                CustomDBus::getCustomDBus().implementObjectEnableIface(
                    entity.first, false);
                break;
            case PLDM_ENTITY_SYSTEM_CHASSIS:
                CustomDBus::getCustomDBus().implementChassisInterface(
                    entity.first);
                CustomDBus::getCustomDBus().implementGlobalInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_POWER_SUPPLY:
                CustomDBus::getCustomDBus().implementPowerSupplyInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_CHASSIS_FRONT_PANEL_BOARD:
                //  CustomDBus::getCustomDBus().implementPanelInterface(
                //    entity.first);
                break;
            case PLDM_ENTITY_FAN:
                CustomDBus::getCustomDBus().implementFanInterface(entity.first);
                break;
            case PLDM_ENTITY_SYS_BOARD:
                CustomDBus::getCustomDBus().implementMotherboardInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_POWER_CONVERTER:
                CustomDBus::getCustomDBus().implementVRMInterface(entity.first);
                break;
            case PLDM_ENTITY_SLOT:
                CustomDBus::getCustomDBus().implementPCIeSlotInterface(
                    entity.first);
                // CustomDBus::getCustomDBus().setLinkReset(
                //   entity.first, false, hostEffecterParser, mctp_eid);
                break;
            case PLDM_ENTITY_CONNECTOR:
                CustomDBus::getCustomDBus().implementConnecterInterface(
                    entity.first);
                CustomDBus::getCustomDBus().implementPortInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_COOLING_DEVICE:
            case PLDM_ENTITY_EXTERNAL_ENVIRONMENT:
            case PLDM_ENTITY_BOARD:
            case PLDM_ENTITY_MODULE:
                CustomDBus::getCustomDBus().implementBoard(entity.first);
                break;
            case PLDM_ENTITY_CARD:
                CustomDBus::getCustomDBus().implementPCIeDeviceInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_IO_MODULE:
                CustomDBus::getCustomDBus().implementFabricAdapter(
                    entity.first);
                CustomDBus::getCustomDBus().implementPCIeDeviceInterface(
                    entity.first);
                break;
            case PLDM_ENTITY_SLOT | 0x8000:
                CustomDBus::getCustomDBus().implementPCIeSlotInterface(
                    entity.first);
                CustomDBus::getCustomDBus().setSlotType(
                    entity.first,
                    "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OEM");
                break;
            default:
                break;
        }
    }
    getFRURecordTableMetadataByRemote();
    this->setFRUDynamicAssociations();

    // update xyz.openbmc_project.State.Decorator.OperationalStatus
    setOperationStatus();
    error("Refreshing dbus hosted by pldm Completed");
}

void HostPDRHandler::deleteDbusObjects(const std::vector<uint16_t> types)
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

        error("Deleting the dbus objects of type : {OBJ_TYP} ", "OBJ_TYP",
              (unsigned)type);
        for (const auto& [path, entites] : savedObjs.at(type))
        {
            if (type !=
                (PLDM_ENTITY_PROC | 0x8000)) // other than CPU core object
            {
                info("Erasing Dbus Path from ObjectMap {DBUS_PATH}",
                     "DBUS_PATH", path.c_str());
                objPathMap.erase(path);
                // Delete the Mex Led Dbus Object paths
                auto ledGroupPath = updateLedGroupPath(path);
                pldm::dbus::CustomDBus::getCustomDBus().deleteObject(
                    ledGroupPath);
            }
            pldm::dbus::CustomDBus::getCustomDBus().deleteObject(path);
        }
    }
}

void HostPDRHandler::setFRUDynamicAssociations()
{
    for (const auto& [leftPath, leftEntity] : objPathMap)
    {
        // for each path, compare it with rest of the paths in the
        // map
        uint16_t leftEntityType = leftEntity.entity_type;
        for (const auto& [rightPath, rightEntity] : objPathMap)
        {
            uint16_t rightEntityType = rightEntity.entity_type;
            // if leftpath is same as rightPath
            // then both dbus objects are same, so
            // no need to create any associations
            if (leftPath == rightPath)
            {
                continue;
            }
            else if ((rightPath.string().find(leftPath) != std::string::npos) ||
                     (leftPath.string().find(rightPath) != std::string::npos))
            {
                // this means left path dbus object is parent of the
                // right path dbus object, something like this
                // leftpath = /xyz/openbmc_project/system/chassis15363
                // rightpath = /xyz/openbmc_project/system/chassis15363/fan1
                auto key = std::make_pair(leftEntityType, rightEntityType);

                if (associationsParser->associationsInfoMap.contains(key))
                {
                    auto value = associationsParser->associationsInfoMap[key];
                    // we have some associations defined for this parent type &
                    // child type
                    std::vector<
                        std::tuple<std::string, std::string, std::string>>
                        associations{{value.first, value.second, rightPath}};
                    CustomDBus::getCustomDBus().setAssociations(leftPath,
                                                                associations);
                }
            }
        }
    }
}

void HostPDRHandler::setOperationStatus()
{
    if (isHostOff)
    {
        // If host is off, then no need to
        // proceed further
        return;
    }
    if (objMapIndex != objPathMap.end())
    {
        pldm_entity node = objMapIndex->second;

        bool valid = getValidity(sensorMapIndex->first.terminusID);

        if (valid)
        {
            pldm::pdr::EntityInfo entityInfo{};
            pldm::pdr::CompositeSensorStates compositeSensorStates{};
            std::vector<pldm::pdr::StateSetId> stateSetIds{};
            std::tie(entityInfo, compositeSensorStates,
                     stateSetIds) = sensorMapIndex->second;
            const auto& [containerId, entityType, entityInstance] = entityInfo;

            if (node.entity_type == entityType &&
                node.entity_instance_num == entityInstance &&
                node.entity_container_id == containerId)
            {
                if ((stateSetIds[0] == PLDM_STATE_SET_HEALTH_STATE ||
                     stateSetIds[0] == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS))
                {
                    // set the dbus property only when its not a
                    // composite sensor and the state set it
                    // PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS Get
                    // sensorOpState property by the
                    // getStateSensorReadings command.

                    getPresentStateBySensorReadigs(
                        sensorMapIndex->first.terminusID,
                        sensorMapIndex->first.sensorID, entityType,
                        entityInstance, containerId, objMapIndex->first,
                        stateSetIds[0]);
                    return;
                }
                if (stateSetIds[0] == PLDM_STATE_SET_IDENTIFY_STATE)
                {
                    getPresentStateBySensorReadigs(
                        sensorMapIndex->first.terminusID,
                        sensorMapIndex->first.sensorID, entityType,
                        entityInstance, containerId, objMapIndex->first,
                        stateSetIds[0]);
                    return;
                }
                ++sensorMapIndex;
                if (sensorMapIndex == sensorMap.end())
                {
                    ++objMapIndex;
                    sensorMapIndex = sensorMap.begin();
                }
                setOperationStatus();
            }
            else
            {
                ++sensorMapIndex;
                if (sensorMapIndex == sensorMap.end())
                {
                    ++objMapIndex;
                    sensorMapIndex = sensorMap.begin();
                }
                setOperationStatus();
            }
        }
        else
        {
            ++sensorMapIndex;
            if (sensorMapIndex == sensorMap.end())
            {
                ++objMapIndex;
                sensorMapIndex = sensorMap.begin();
            }
            setOperationStatus();
        }
    }
}

std::string HostPDRHandler::getParentChassis(const std::string& frupath)
{
    if (objPathMap.contains(frupath))
    {
        pldm_entity fruentity = objPathMap[frupath];
        if (fruentity.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
        {
            return "";
        }
    }
    for (const auto& [objpath, nodeentity] : objPathMap)
    {
        pldm_entity entity = nodeentity;
        if (frupath.find(objpath) != std::string::npos &&
            entity.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
        {
            return objpath;
        }
    }
    return "";
}

void HostPDRHandler::setRecordPresent(uint32_t recordHandle)
{
    pldm_entity recordEntity = pldm_get_entity_from_record_handle(repo,
                                                                  recordHandle);
    for (const auto& [path, dbusEntity] : objPathMap)
    {
        if (dbusEntity.entity_type == recordEntity.entity_type &&
            dbusEntity.entity_instance_num ==
                recordEntity.entity_instance_num &&
            dbusEntity.entity_container_id == recordEntity.entity_container_id)
        {
            error(
                "Removing Host FRU [ {PATH} ] with entityid [ {ENTITY_TYP}, {ENTITY_NUM}, {ENTITY_ID} ]",
                "PATH", path, "ENTITY_TYP", (unsigned)recordEntity.entity_type,
                "ENTITY_NUM", (unsigned)recordEntity.entity_instance_num,
                "ENTITY_ID", (unsigned)recordEntity.entity_container_id);
            // if the record has the same entity id, mark that dbus object as
            // not present
            CustomDBus::getCustomDBus().updateItemPresentStatus(path, false);
            CustomDBus::getCustomDBus().setOperationalStatus(
                path, false, getParentChassis(path));
            // Delete the LED object path
            auto ledGroupPath = updateLedGroupPath(path);
            pldm::dbus::CustomDBus::getCustomDBus().deleteObject(ledGroupPath);
            return;
        }
    }
}

std::string HostPDRHandler::updateLedGroupPath(const std::string& path)
{
    std::string ledGroupPath{};
    std::string inventoryPath = "/xyz/openbmc_project/inventory/";
    if (path.find(inventoryPath) != std::string::npos)
    {
        ledGroupPath = "/xyz/openbmc_project/led/groups/" +
                       path.substr(inventoryPath.length());
    }

    return ledGroupPath;
}

void HostPDRHandler::deletePDRFromRepo(PDRRecordHandles&& recordHandles)
{
    for (auto& recordHandle : recordHandles)
    {
        error("Record handle deleted: {REC_HANDLE}", "REC_HANDLE",
              recordHandle);
        this->setRecordPresent(recordHandle);
        pldm_delete_by_record_handle(repo, recordHandle, true);
    }
}

} // namespace pldm
