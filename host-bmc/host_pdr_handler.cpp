#include "config.h"

#include "host_pdr_handler.hpp"

#include "libpldm/fru.h"
#include "libpldm/requester/pldm.h"
#include "libpldm/state_set.h"
#include "oem/ibm/libpldm/fru.h"

#include "custom_dbus.hpp"

#include <assert.h>

#include <nlohmann/json.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/time.hpp>

#include <fstream>
#include <type_traits>

namespace pldm
{

using namespace pldm::dbus_api;
using namespace pldm::responder::events;
using namespace pldm::utils;
using namespace sdbusplus::bus::match::rules;
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace pldm::dbus;
constexpr auto fruJson = "host_frus.json";
const Json emptyJson{};
const std::vector<Json> emptyJsonList{};

template <typename T>
void updateContanierId(pldm_entity_association_tree* entityTree,
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
    auto node = pldm_entity_association_tree_find(entityTree, &entity, true);
    if (node)
    {
        pldm_entity e = pldm_entity_extract(node);
        t->container_id = e.entity_container_id;
    }
}

HostPDRHandler::HostPDRHandler(
    int mctp_fd, uint8_t mctp_eid, sdeventplus::Event& event, pldm_pdr* repo,
    const std::string& eventsJsonsDir, pldm_entity_association_tree* entityTree,
    pldm_entity_association_tree* bmcEntityTree, Requester& requester,
    pldm::requester::Handler<pldm::requester::Request>* handler,
    pldm::responder::oem_platform::Handler* oemPlatformHandler) :
    mctp_fd(mctp_fd),
    mctp_eid(mctp_eid), event(event), repo(repo),
    stateSensorHandler(eventsJsonsDir), entityTree(entityTree),
    bmcEntityTree(bmcEntityTree), requester(requester), handler(handler),
    oemPlatformHandler(oemPlatformHandler)
{
    mergedHostParents = false;
    fs::path hostFruJson(fs::path(HOST_JSONS_DIR) / fruJson);
    if (fs::exists(hostFruJson))
    {
        // Note parent entities for entities sent down by the host firmware.
        // This will enable a merge of entity associations.
        try
        {
            std::ifstream jsonFile(hostFruJson);
            auto data = Json::parse(jsonFile, nullptr, false);
            if (data.is_discarded())
            {
                std::cerr << "Parsing Host FRU json file failed" << std::endl;
            }
            else
            {
                auto entities = data.value("entities", emptyJsonList);
                for (auto& entity : entities)
                {
                    EntityType entityType = entity.value("entity_type", 0);
                    auto parent = entity.value("parent", emptyJson);
                    pldm_entity p{};
                    p.entity_type = parent.value("entity_type", 0);
                    p.entity_instance_num = parent.value("entity_instance", 0);
                    parents.emplace(entityType, std::move(p));
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Parsing Host FRU json file failed, exception = "
                      << e.what() << std::endl;
        }
    }

    hostOffMatch = std::make_unique<sdbusplus::bus::match::match>(
        pldm::utils::DBusHandler::getBus(),
        propertiesChanged("/xyz/openbmc_project/state/host0",
                          "xyz.openbmc_project.State.Host"),
        [this, repo, entityTree,
         bmcEntityTree](sdbusplus::message::message& msg) {
            DbusChangedProps props{};
            std::string intf;
            msg.read(intf, props);
            const auto itr = props.find("CurrentHostState");
            if (itr != props.end())
            {
                PropertyValue value = itr->second;
                auto propVal = std::get<std::string>(value);
                if (propVal == "xyz.openbmc_project.State.Host.HostState.Off")
                {
                    pldm_pdr_remove_remote_pdrs(repo);
                    pldm_entity_association_tree_destroy_root(entityTree);
                    pldm_entity_association_tree_copy_root(bmcEntityTree,
                                                           entityTree);
                    this->sensorMap.clear();
                    this->responseReceived = false;
                    this->mergedHostParents = false;
                    this->sensorMapIndex = objPathMap.begin();
                }
            }
        });
}

void HostPDRHandler::fetchPDR(PDRRecordHandles&& recordHandles)
{
    pdrRecordHandles.clear();
    pdrRecordHandles = std::move(recordHandles);

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
    if (!nextRecordHandle)
    {
        if (!pdrRecordHandles.empty())
        {
            recordHandle = pdrRecordHandles.front();
            pdrRecordHandles.pop_front();
        }
    }
    else
    {
        recordHandle = nextRecordHandle;
    }
    auto instanceId = requester.getInstanceId(mctp_eid);

    auto rc =
        encode_get_pdr_req(instanceId, recordHandle, 0, PLDM_GET_FIRSTPART,
                           UINT16_MAX, 0, request, PLDM_GET_PDR_REQ_BYTES);
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(mctp_eid, instanceId);
        std::cerr << "Failed to encode_get_pdr_req, rc = " << rc << std::endl;
        return;
    }

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_GET_PDR,
        std::move(requestMsg),
        std::move(std::bind_front(&HostPDRHandler::processHostPDRs, this)));
    if (rc)
    {
        std::cerr << "Failed to send the GetPDR request to Host \n";
    }
}

int HostPDRHandler::handleStateSensorEvent(const StateSensorEntry& entry,
                                           pdr::EventState state)
{
    for (auto& entity : objPathMap)
    {
        pldm_entity node_entity = pldm_entity_extract(entity.second);

        if (node_entity.entity_type != entry.entityType ||
            node_entity.entity_instance_num != entry.entityInstance ||
            node_entity.entity_container_id != entry.containerId)
        {
            continue;
        }

        CustomDBus::getCustomDBus().setOperationalStatus(entity.first, state);
        break;
    }

    auto rc = stateSensorHandler.eventAction(entry, state);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Failed to fetch and update D-bus property, rc = " << rc
                  << std::endl;
        return rc;
    }

    return PLDM_SUCCESS;
}

void HostPDRHandler::mergeEntityAssociations(const std::vector<uint8_t>& pdr)
{
    size_t numEntities{};
    pldm_entity* entities = nullptr;
    bool merged = false;
    auto entityPdr = reinterpret_cast<pldm_pdr_entity_association*>(
        const_cast<uint8_t*>(pdr.data()) + sizeof(pldm_pdr_hdr));

    pldm_entity_association_pdr_extract(pdr.data(), pdr.size(), &numEntities,
                                        &entities);
    if (numEntities > 0)
    {
        pldm_entity_node* pNode;
        if (!mergedHostParents)
        {
            pNode = pldm_entity_association_tree_find(entityTree, &entities[0],
                                                      false);
        }
        else
        {
            pNode = pldm_entity_association_tree_find(entityTree, &entities[0],
                                                      true);
        }
        if (!pNode)
        {
            return;
        }

        Entities entityAssoc;
        entityAssoc.push_back(pNode);
        for (size_t i = 1; i < numEntities; ++i)
        {
            auto node = pldm_entity_association_tree_add(
                entityTree, &entities[i], entities[i].entity_instance_num,
                pNode, entityPdr->association_type, true);
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
            std::cerr
                << "\ncould not find referrence of the entity in the tree \n";
        }
        else
        {
            pldm_entity_association_pdr_add_from_node(node, repo, &entities,
                                                      numEntities, true);
        }
    }
    free(entities);
}

void HostPDRHandler::sendPDRRepositoryChgEvent(std::vector<uint8_t>&& pdrTypes,
                                               uint8_t eventDataFormat)
{
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
                changeEntries[0].push_back(
                    pldm_pdr_get_record_handle(repo, record));
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
        std::cerr
            << "Failed to encode_pldm_pdr_repository_chg_event_data, rc = "
            << rc << std::endl;
        return;
    }
    auto instanceId = requester.getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES +
                                    actualSize);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    rc = encode_platform_event_message_req(
        instanceId, 1, 0, PLDM_PDR_REPOSITORY_CHG_EVENT, eventDataVec.data(),
        actualSize, request,
        actualSize + PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES);
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(mctp_eid, instanceId);
        std::cerr << "Failed to encode_platform_event_message_req, rc = " << rc
                  << std::endl;
        return;
    }

    auto platformEventMessageResponseHandler = [](mctp_eid_t /*eid*/,
                                                  const pldm_msg* response,
                                                  size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for the PDR repository "
                         "changed event"
                      << "\n";
            return;
        }

        uint8_t completionCode{};
        uint8_t status{};
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_platform_event_message_resp(responsePtr, respMsgLen,
                                                     &completionCode, &status);
        if (rc || completionCode)
        {
            std::cerr << "Failed to decode_platform_event_message_resp: "
                      << "rc=" << rc
                      << ", cc=" << static_cast<unsigned>(completionCode)
                      << std::endl;
        }
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_PLATFORM_EVENT_MESSAGE,
        std::move(requestMsg), std::move(platformEventMessageResponseHandler));
    if (rc)
    {
        std::cerr << "Failed to send the PDR repository changed event request"
                  << "\n";
    }
}

void HostPDRHandler::parseStateSensorPDRs(const PDRList& stateSensorPDRs,
                                          const TLPDRMap& tlpdrInfo)
{
    for (const auto& pdr : stateSensorPDRs)
    {
        SensorEntry sensorEntry{};
        const auto& [terminusHandle, sensorID, sensorInfo] =
            responder::pdr_utils::parseStateSensorPDR(pdr);
        sensorEntry.sensorID = sensorID;
        try
        {
            sensorEntry.terminusID = tlpdrInfo.at(terminusHandle);
        }
        // If there is no mapping for terminusHandle assign the reserved TID
        // value of 0xFF to indicate that.
        catch (const std::out_of_range& e)
        {
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
    static PDRList stateSensorPDRs{};
    static PDRList fruRecordSetPDRs{};
    static TLPDRMap tlpdrInfo{};
    uint32_t nextRecordHandle{};
    std::vector<TlInfo> tlInfo;
    uint8_t tlEid = 0;
    bool tlValid = true;
    uint32_t rh = 0;
    uint16_t terminusHandle = 0;
    uint8_t tid = 0;

    uint8_t completionCode{};
    uint32_t nextDataTransferHandle{};
    uint8_t transferFlag{};
    uint16_t respCount{};
    uint8_t transferCRC{};
    if (response == nullptr || !respMsgLen)
    {
        std::cerr << "Failed to receive response for the GetPDR"
                     " command \n";
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
        std::cerr << "Failed to decode_get_pdr_resp, rc = " << rc << std::endl;
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
            std::cerr << "Failed to decode_get_pdr_resp: "
                      << "rc=" << rc
                      << ", cc=" << static_cast<unsigned>(completionCode)
                      << std::endl;
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
                this->mergeEntityAssociations(pdr);
                merged = true;
            }
            else
            {
                if (pdrHdr->type == PLDM_TERMINUS_LOCATOR_PDR)
                {
                    auto tlpdr =
                        reinterpret_cast<const pldm_terminus_locator_pdr*>(
                            pdr.data());
                    tlpdrInfo.emplace(
                        static_cast<pldm::pdr::TerminusHandle>(
                            tlpdr->terminus_handle),
                        static_cast<pldm::pdr::TerminusID>(tlpdr->tid));

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
                        tlValid = false;
                    }
                    tlInfo.emplace_back(
                        TlInfo{tlpdr->validity, static_cast<uint8_t>(tlEid),
                               tlpdr->tid, tlpdr->terminus_handle});
                }
                else if (pdrHdr->type == PLDM_STATE_SENSOR_PDR)
                {
                    updateContanierId<pldm_state_sensor_pdr>(entityTree, pdr);
                    stateSensorPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_PDR_FRU_RECORD_SET)
                {
                    updateContanierId<pldm_pdr_fru_record_set>(entityTree, pdr);
                    fruRecordSetPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_STATE_EFFECTER_PDR)
                {
                    updateContanierId<pldm_state_effecter_pdr>(entityTree, pdr);
                }

                // if the TLPDR is invalid update the repo accordingly
                if (!tlValid)
                {
                    pldm_pdr_update_TL_pdr(repo, terminusHandle, tid, tlEid,
                                           tlValid);
                }
                else
                {
                    pldm_pdr_add(repo, pdr.data(), respCount, rh, true);
                }
            }
        }
    }
    if (!nextRecordHandle)
    {
        pldm::hostbmc::utils::updateEntityAssociation(
            entityAssociations, entityTree, objPathMap, oemPlatformHandler);

        if (oemPlatformHandler != nullptr)
        {
            pldm::hostbmc::utils::setCoreCount(entityAssociations);
        }

        /*received last record*/
        this->parseStateSensorPDRs(stateSensorPDRs, tlpdrInfo);
        this->parseFruRecordSetPDRs(fruRecordSetPDRs);
        if (isHostUp())
        {
            this->setHostSensorState(stateSensorPDRs, tlInfo);
        }
        stateSensorPDRs.clear();
        tlpdrInfo.clear();
        fruRecordSetPDRs.clear();
        entityAssociations.clear();

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
        deferredFetchPDREvent = std::make_unique<sdeventplus::source::Defer>(
            event,
            std::bind(std::mem_fn((&HostPDRHandler::_processFetchPDREvent)),
                      this, nextRecordHandle, std::placeholders::_1));
    }
}

void HostPDRHandler::_processPDRRepoChgEvent(
    sdeventplus::source::EventBase& /*source */)
{
    if (oemPlatformHandler != nullptr)
    {
        oemPlatformHandler->updateContainerID();
    }
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
    this->getHostPDR(nextRecordHandle);
}

void HostPDRHandler::setHostFirmwareCondition()
{
    responseReceived = false;
    auto instanceId = requester.getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_VERSION_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_version_req(instanceId, 0, PLDM_GET_FIRSTPART,
                                     PLDM_BASE, request);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "GetPLDMVersion encode failure. PLDM error code = "
                  << std::hex << std::showbase << rc << "\n";
        requester.markFree(mctp_eid, instanceId);
        return;
    }

    auto getPLDMVersionHandler = [this](mctp_eid_t /*eid*/,
                                        const pldm_msg* response,
                                        size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for "
                      << "getPLDMVersion command, Host seems to be off \n";
            return;
        }
        std::cout << "Getting the response. PLDM RC = " << std::hex
                  << std::showbase
                  << static_cast<uint16_t>(response->payload[0]) << "\n";
        this->responseReceived = true;
        getHostPDR();
    };
    rc = handler->registerRequest(mctp_eid, instanceId, PLDM_BASE,
                                  PLDM_GET_PLDM_VERSION, std::move(requestMsg),
                                  std::move(getPLDMVersionHandler));
    if (rc)
    {
        std::cerr << "Failed to discover Host state. Assuming Host as off \n";
    }
}

bool HostPDRHandler::isHostUp()
{
    return responseReceived;
}

void HostPDRHandler::setHostSensorState(const PDRList& stateSensorPDRs,
                                        const std::vector<TlInfo>& tlinfo)
{
    for (const auto& stateSensorPDR : stateSensorPDRs)
    {
        auto pdr = reinterpret_cast<const pldm_state_sensor_pdr*>(
            stateSensorPDR.data());

        if (!pdr)
        {
            std::cerr << "Failed to get State sensor PDR" << std::endl;
            pldm::utils::reportError(
                "xyz.openbmc_project.bmc.pldm.InternalFailure");
            return;
        }

        uint16_t sensorId = pdr->sensor_id;

        for (auto info : tlinfo)
        {
            if (info.terminusHandle == pdr->terminus_handle)
            {
                if (info.valid == PLDM_TL_PDR_VALID)
                {
                    mctp_eid = info.eid;
                }

                bitfield8_t sensorRearm;
                sensorRearm.byte = 0;
                uint8_t tid = info.tid;

                auto instanceId = requester.getInstanceId(mctp_eid);
                std::vector<uint8_t> requestMsg(
                    sizeof(pldm_msg_hdr) +
                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);
                auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
                auto rc = encode_get_state_sensor_readings_req(
                    instanceId, sensorId, sensorRearm, 0, request);

                if (rc != PLDM_SUCCESS)
                {
                    requester.markFree(mctp_eid, instanceId);
                    std::cerr << "Failed to "
                                 "encode_get_state_sensor_readings_req, rc = "
                              << rc << std::endl;
                    pldm::utils::reportError(
                        "xyz.openbmc_project.bmc.pldm.InternalFailure");
                    return;
                }

                auto getStateSensorReadingRespHandler = [=, this](
                                                            mctp_eid_t /*eid*/,
                                                            const pldm_msg*
                                                                response,
                                                            size_t respMsgLen) {
                    if (response == nullptr || !respMsgLen)
                    {
                        std::cerr << "Failed to receive response for "
                                     "getStateSensorReading command \n";
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
                        std::cerr
                            << "Failed to "
                               "decode_get_state_sensor_readings_resp, rc = "
                            << rc
                            << " cc=" << static_cast<unsigned>(completionCode)
                            << std::endl;
                        pldm::utils::reportError(
                            "xyz.openbmc_project.bmc.pldm.InternalFailure");
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
                        catch (const std::out_of_range& e)
                        {
                            try
                            {
                                sensorEntry.terminusID = PLDM_TID_RESERVED;
                                std::tie(entityInfo, compositeSensorStates,
                                         stateSetIds) =
                                    lookupSensorInfo(sensorEntry);
                            }
                            catch (const std::out_of_range& e)
                            {
                                std::cerr << "No mapping for the events"
                                          << std::endl;
                            }
                        }

                        if (sensorOffset > compositeSensorStates.size())
                        {
                            std::cerr
                                << " Error Invalid data, Invalid sensor offset"
                                << std::endl;
                            return;
                        }

                        const auto& possibleStates =
                            compositeSensorStates[sensorOffset];
                        if (possibleStates.find(eventState) ==
                            possibleStates.end())
                        {
                            std::cerr
                                << " Error invalid_data, Invalid event state"
                                << std::endl;
                            return;
                        }
                        const auto& [containerId, entityType, entityInstance] =
                            entityInfo;
                        pldm::responder::events::StateSensorEntry
                            stateSensorEntry{containerId, entityType,
                                             entityInstance, sensorOffset};
                        handleStateSensorEvent(stateSensorEntry, eventState);
                    }
                };

                rc = handler->registerRequest(
                    mctp_eid, instanceId, PLDM_PLATFORM,
                    PLDM_GET_STATE_SENSOR_READINGS, std::move(requestMsg),
                    std::move(getStateSensorReadingRespHandler));

                if (rc != PLDM_SUCCESS)
                {
                    std::cerr << " Failed to send request to get State sensor "
                                 "reading on Host "
                              << std::endl;
                }
            }
        }
    }
}

void HostPDRHandler::getFRURecordTableMetadataByHost(
    const PDRList& fruRecordSetPDRs)
{
    auto instanceId = requester.getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(
        sizeof(pldm_msg_hdr) + PLDM_GET_FRU_RECORD_TABLE_METADATA_REQ_BYTES);

    // GetFruRecordTableMetadata
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_fru_record_table_metadata_req(
        instanceId, request, requestMsg.size() - sizeof(pldm_msg_hdr));
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(mctp_eid, instanceId);
        std::cerr << "Failed to encode_get_fru_record_table_metadata_req, rc = "
                  << rc << std::endl;
        return;
    }

    auto getFruRecordTableMetadataResponseHandler = [this, fruRecordSetPDRs](
                                                        mctp_eid_t /*eid*/,
                                                        const pldm_msg*
                                                            response,
                                                        size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for the Get FRU Record "
                         "Table Metadata\n";
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
            std::cerr << "Faile to decode get fru record table metadata resp, "
                         "Message Error: "
                      << "rc=" << rc << ",cc=" << (int)cc << std::endl;
            return;
        }

        // pass total to getFRURecordTableByHost
        this->getFRURecordTableByHost(total, fruRecordSetPDRs);
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_FRU, PLDM_GET_FRU_RECORD_TABLE_METADATA,
        std::move(requestMsg),
        std::move(getFruRecordTableMetadataResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        std::cerr
            << "Failed to send the the Set State Effecter States request\n";
    }

    return;
}

void HostPDRHandler::getFRURecordTableByHost(uint16_t& total_table_records,
                                             const PDRList& fruRecordSetPDRs)
{
    fruRecordData.clear();

    if (!total_table_records)
    {
        std::cerr << "Failed to get fru record table." << std::endl;
        return;
    }

    auto instanceId = requester.getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_FRU_RECORD_TABLE_REQ_BYTES);

    // send the getFruRecordTable command
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto rc = encode_get_fru_record_table_req(
        instanceId, 0, PLDM_GET_FIRSTPART, request,
        requestMsg.size() - sizeof(pldm_msg_hdr));
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(mctp_eid, instanceId);
        std::cerr << "Failed to encode_get_fru_record_table_req, rc = " << rc
                  << std::endl;
        return;
    }

    auto getFruRecordTableResponseHandler = [total_table_records, this,
                                             fruRecordSetPDRs](
                                                mctp_eid_t /*eid*/,
                                                const pldm_msg* response,
                                                size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for the Get FRU Record "
                         "Table\n";
            return;
        }

        uint8_t cc = 0;
        uint32_t next_data_transfer_handle = 0;
        uint8_t transfer_flag = 0;
        size_t fru_record_table_length = 0;
        std::vector<uint8_t> fru_record_table_data(respMsgLen -
                                                   sizeof(pldm_msg_hdr));
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_get_fru_record_table_resp(
            responsePtr, respMsgLen - sizeof(pldm_msg_hdr), &cc,
            &next_data_transfer_handle, &transfer_flag,
            fru_record_table_data.data(), &fru_record_table_length);

        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            std::cerr
                << "Failed to decode get fru record table resp, Message Error: "
                << "rc=" << rc << ",cc=" << (int)cc << std::endl;
            return;
        }

        fruRecordData = responder::pdr_utils::parseFruRecordTable(
            fru_record_table_data.data(), fru_record_table_length);

        if (total_table_records != fruRecordData.size())
        {
            fruRecordData.clear();

            std::cerr << "failed to parse fru recrod data format.\n";
            return;
        }

        sensorMapIndex = objPathMap.begin();

        // update xyz.openbmc_project.State.Decorator.OperationalStatus
        setOperationStatus();

        for (auto& entity : objPathMap)
        {
            pldm_entity node = pldm_entity_extract(entity.second);
            auto fruRSI = getRSI(fruRecordSetPDRs, node);

            // update the Present Property
            setPresentPropertyStatus(entity.first);
            if (node.entity_type == (PLDM_ENTITY_PROC | 0x8000))
            {
                CustomDBus::getCustomDBus().implementCpuCoreInterface(
                    entity.first);
            }

            for (auto& data : fruRecordData)
            {
                if (fruRSI != data.fruRSI)
                {
                    continue;
                }

                if (data.fruRecType == PLDM_FRU_RECORD_TYPE_OEM)
                {
                    for (auto& tlv : data.fruTLV)
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
            }
        }
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_FRU, PLDM_GET_FRU_RECORD_TABLE,
        std::move(requestMsg), std::move(getFruRecordTableResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        std::cerr
            << "Failed to send the the Set State Effecter States request\n";
    }
}

void HostPDRHandler::getPresentStateBySensorReadigs(uint16_t sensorId,
                                                    uint8_t state,
                                                    const std::string& path)
{

    auto instanceId = requester.getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);

    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    bitfield8_t bf;
    bf.byte = 0;
    auto rc = encode_get_state_sensor_readings_req(instanceId, sensorId, bf, 0,
                                                   request);
    if (rc != PLDM_SUCCESS)
    {
        requester.markFree(mctp_eid, instanceId);
        std::cerr << "Failed to encode_get_state_sensor_readings_req, rc = "
                  << rc << std::endl;
        state = PLDM_OPERATIONAL_NON_RECOVERABLE_ERROR;
        return;
    }

    state = PLDM_OPERATIONAL_ERROR;
    auto getStateSensorReadingsResponseHandler = [this, path, &state](
                                                     mctp_eid_t /*eid*/,
                                                     const pldm_msg* response,
                                                     size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr << "Failed to receive response for the Get FRU Record "
                         "Table\n";
            return;
        }

        uint8_t cc = 0;
        uint8_t sensorCnt = 0;
        std::array<get_sensor_state_field, 8> stateField{};
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_get_state_sensor_readings_resp(
            responsePtr, respMsgLen - sizeof(pldm_msg_hdr), &cc, &sensorCnt,
            stateField.data());
        if (rc != PLDM_SUCCESS || cc != PLDM_SUCCESS)
        {
            std::cerr << "Faile to decode get state sensor readings resp, "
                         "Message Error: "
                      << "rc=" << rc << ",cc=" << (int)cc << std::endl;
            state = PLDM_OPERATIONAL_NON_RECOVERABLE_ERROR;
            return;
        }

        for (const auto& filed : stateField)
        {
            if (filed.present_state == PLDM_SENSOR_NORMAL)
            {
                state = PLDM_OPERATIONAL_NORMAL;
                break;
            }
        }
        CustomDBus::getCustomDBus().setOperationalStatus(path, state);

        if (++sensorMapIndex != objPathMap.end())
        {
            setOperationStatus();
        }
    };

    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_GET_STATE_SENSOR_READINGS,
        std::move(requestMsg),
        std::move(getStateSensorReadingsResponseHandler));
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Failed to get the State Sensor Readings request\n";
    }

    return;
}

uint16_t HostPDRHandler::getRSI(const PDRList& fruRecordSetPDRs,
                                const pldm_entity& entity)
{
    uint16_t fruRSI = 0;

    for (const auto& pdr : fruRecordSetPDRs)
    {
        auto fruPdr = reinterpret_cast<const pldm_pdr_fru_record_set*>(
            const_cast<uint8_t*>(pdr.data()) + sizeof(pldm_pdr_hdr));

        if (fruPdr->entity_type == entity.entity_type &&
            fruPdr->entity_instance == entity.entity_instance_num &&
            fruPdr->container_id == entity.entity_container_id)
        {
            fruRSI = fruPdr->fru_rsi;
            break;
        }
    }

    return fruRSI;
}

void HostPDRHandler::setOperationStatus()
{
    if (sensorMapIndex != objPathMap.end())
    {

        pldm_entity node = pldm_entity_extract(sensorMapIndex->second);

        for (auto& sensor : sensorMap)
        {
            pldm::pdr::EntityInfo entityInfo{};
            pldm::pdr::CompositeSensorStates compositeSensorStates{};
            std::vector<pldm::pdr::StateSetId> stateSetIds{};
            std::tie(entityInfo, compositeSensorStates, stateSetIds) =
                sensor.second;
            const auto& [containerId, entityType, entityInstance] = entityInfo;

            if (node.entity_type != entityType ||
                node.entity_instance_num != entityInstance ||
                node.entity_container_id != containerId)
            {
                continue;
            }
            uint8_t state = 0;
            if (compositeSensorStates.size() == 1 &&
                stateSetIds[0] == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS)
            {
                // set the dbus property only when its not a composite sensor
                // and the state set it PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS
                // Get sensorOpState property by the getStateSensorReadings
                // command.
                getPresentStateBySensorReadigs(sensor.first.sensorID, state,
                                               sensorMapIndex->first);
            }
        }
    }
}

void HostPDRHandler::setPresentPropertyStatus(const std::string& path)
{
    CustomDBus::getCustomDBus().updateItemPresentStatus(path);
}

void HostPDRHandler::parseFruRecordSetPDRs(const PDRList& fruRecordSetPDRs)
{
    getFRURecordTableMetadataByHost(fruRecordSetPDRs);
}

} // namespace pldm
