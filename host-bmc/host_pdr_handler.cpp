#include "host_pdr_handler.hpp"

#include "libpldm/fru.h"
#include "libpldm/pldm.h"
#include "libpldm/state_set.h"
#ifdef OEM_IBM
#include "libpldm/fru_oem_ibm.h"
#include "libpldm/pdr_oem_ibm.h"
#endif

#include "dbus/custom_dbus.hpp"
#include "dbus/serialize.hpp"
#include "host-bmc/dbus/deserialize.hpp"

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
using namespace pldm::responder::events;
using namespace pldm::utils;
using namespace sdbusplus::bus::match::rules;
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
    pldm_entity_association_tree* bmcEntityTree,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser,
    pldm::InstanceIdDb& instanceIdDb,
    pldm::requester::Handler<pldm::requester::Request>* handler,
    pldm::host_associations::HostAssociationsParser* associationsParser,
    pldm::responder::oem_platform::Handler* oemPlatformHandler) :
    mctp_fd(mctp_fd),
    mctp_eid(mctp_eid), event(event), repo(repo),
    stateSensorHandler(eventsJsonsDir), entityTree(entityTree),
    bmcEntityTree(bmcEntityTree), hostEffecterParser(hostEffecterParser),
    instanceIdDb(instanceIdDb), handler(handler),
    associationsParser(associationsParser),
    oemPlatformHandler(oemPlatformHandler)
{
    isHostOff = false;
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
        [this, repo, entityTree, bmcEntityTree,
         oemPlatformHandler](sdbusplus::message::message& msg) {
        DbusChangedProps props{};
        std::string intf;
        msg.read(intf, props);
        const auto itr = props.find("CurrentHostState");
        if (itr != props.end())
        {
            utils::PropertyValue value = itr->second;
            auto propVal = std::get<std::string>(value);
            if (propVal == "xyz.openbmc_project.State.Host.HostState.Off")
            {
                if (oemPlatformHandler != nullptr)
                {
                    oemPlatformHandler->startStopTimer(false);
                }

                for (auto it = this->tlPDRInfo.cbegin();
                     it != this->tlPDRInfo.cend();)
                {
                    if (it->first != TERMINUS_HANDLE)
                    {
                        this->tlPDRInfo.erase(it++);
                    }
                    else
                    {
                        ++it;
                    }
                }

                // when the host is powered off, set the availability
                // state of all the dbus objects to false
                this->setPresenceFrus();
                pldm_pdr_remove_remote_pdrs(repo);
                pldm_entity_association_tree_destroy_root(entityTree);
                pldm_entity_association_tree_copy_root(bmcEntityTree,
                                                       entityTree);
                this->sensorMap.clear();
                this->stateSensorPDRs.clear();
                this->responseReceived = false;
                this->mergedHostParents = false;
                this->objMapIndex = objPathMap.begin();
                this->sensorIndex = stateSensorPDRs.begin();
                this->isHostPdrModified = false;
                this->modifiedCounter = 0;
                fruRecordSetPDRs.clear();

                // After a power off , the remote notes will be deleted
                // from the entity association tree, making the nodes point
                // to junk values, so set them to nullptr
                for (const auto& element : this->objPathMap)
                {
                    this->objPathMap[element.first] = nullptr;
                }
                isHostOff = true;
            }
            else if (propVal ==
                     "xyz.openbmc_project.State.Host.HostState.Running")
            {
                isHostOff = false;
                if (oemPlatformHandler != nullptr)
                {
                    oemPlatformHandler->handleBootTypesAtPowerOn();
                }
            }
        }
        });

    chassisOffMatch = std::make_unique<sdbusplus::bus::match::match>(
        pldm::utils::DBusHandler::getBus(),
        propertiesChanged("/xyz/openbmc_project/state/chassis0",
                          "xyz.openbmc_project.State.Chassis"),
        [this, oemPlatformHandler](sdbusplus::message::message& msg) {
        DbusChangedProps props{};
        std::string intf;
        msg.read(intf, props);
        const auto itr = props.find("CurrentPowerState");
        if (itr != props.end())
        {
            utils::PropertyValue value = itr->second;
            auto propVal = std::get<std::string>(value);
            if (propVal == "xyz.openbmc_project.State.Chassis.PowerState.Off")
            {
                if (oemPlatformHandler)
                {
                    oemPlatformHandler->startStopTimer(false);
                    oemPlatformHandler->handleBootTypesAtChassisOff();
                }
                static constexpr auto searchpath =
                    "/xyz/openbmc_project/inventory/system/chassis/motherboard";
                int depth = 0;
                std::vector<std::string> powerInterface = {
                    "xyz.openbmc_project.State.Decorator.PowerState"};
                pldm::utils::GetSubTreeResponse response =
                    pldm::utils::DBusHandler().getSubtree(searchpath, depth,
                                                          powerInterface);
                for (const auto& [objPath, serviceMap] : response)
                {
                    pldm::utils::DBusMapping dbusMapping{
                        objPath,
                        "xyz.openbmc_project.State.Decorator.PowerState",
                        "PowerState", "string"};
                    value =
                        "xyz.openbmc_project.State.Decorator.PowerState.State.Off";
                    try
                    {
                        pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                                   value);
                    }
                    catch (const std::exception& e)
                    {
                        std::cerr
                            << "Unable to set the slot power state to Off "
                            << "ERROR=" << e.what() << "\n";
                    }
                }
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

std::string HostPDRHandler::getParentChassis(const std::string& frupath)
{
    if (objPathMap.contains(frupath))
    {
        auto node = objPathMap[frupath];
        pldm_entity fruentity = pldm_entity_extract(node);
        if (fruentity.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
        {
            return "";
        }
    }
    for (const auto& [objpath, nodeentity] : objPathMap)
    {
        pldm_entity entity = pldm_entity_extract(nodeentity);
        if (frupath.find(objpath) != std::string::npos &&
            entity.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
        {
            return objpath;
        }
    }
    return "";
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
        event, std::bind_front(std::mem_fn(&HostPDRHandler::_fetchPDR), this));
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

int HostPDRHandler::handleStateSensorEvent(
    const std::vector<pldm::pdr::StateSetId>& stateSetId,
    const StateSensorEntry& entry, pdr::EventState state)
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

        for (const auto& setId : stateSetId)
        {
            if (setId == PLDM_STATE_SET_IDENTIFY_STATE)
            {
                auto ledGroupPath = updateLedGroupPath(entity.first);
                if (!ledGroupPath.empty())
                {
                    std::cout
                        << "led state event for [ " << ledGroupPath << " ] , [ "
                        << node_entity.entity_type << " ,"
                        << node_entity.entity_instance_num << " ,"
                        << node_entity.entity_container_id
                        << " ] , current value : [ " << std::boolalpha
                        << CustomDBus::getCustomDBus().getAsserted(ledGroupPath)
                        << " ] new value : [ "
                        << bool(state == PLDM_STATE_SET_IDENTIFY_STATE_ASSERTED)
                        << " ]\n";
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
            if (!(state == PLDM_OPERATIONAL_NORMAL) &&
                stateSetId[0] == PLDM_STATE_SET_HEALTH_STATE &&
                strstr(entity.first.c_str(), "core"))
            {
                std::cerr << "Guard event on CORE : [" << entity.first
                          << "] \n";
            }
            CustomDBus::getCustomDBus().setOperationalStatus(
                entity.first, state == PLDM_OPERATIONAL_NORMAL,
                getParentChassis(entity.first));

            break;
        }
        else if (stateSetId[0] == PLDM_STATE_SET_VERSION)
        {
            // There is a version changed on any of the dbus objects
            std::cout
                << "Got a signal from Host about a possible change in Version\n";
            createDbusObjects();
            return PLDM_SUCCESS;
        }
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

void HostPDRHandler::mergeEntityAssociations(
    const std::vector<uint8_t>& pdr, [[maybe_unused]] const uint32_t& size,
    [[maybe_unused]] const uint32_t& record_handle)
{
    size_t numEntities{};
    pldm_entity* entities = nullptr;
    bool merged = false;
    auto entityPdr = reinterpret_cast<pldm_pdr_entity_association*>(
        const_cast<uint8_t*>(pdr.data()) + sizeof(pldm_pdr_hdr));

#ifdef OEM_IBM
    if (oemPlatformHandler && isHBRange(record_handle))
    {
        // Adding the HostBoot range PDRs to the repo before merging it
        pldm_pdr_add(repo, pdr.data(), size, record_handle, true, 0xFFFF);
    }
#endif

    pldm_entity_association_pdr_extract(pdr.data(), pdr.size(), &numEntities,
                                        &entities);
    if (numEntities > 0)
    {
        pldm_entity_node* pNode = nullptr;
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
            std::cerr
                << "\ncould not find referrence of the entity in the tree \n";
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

            // excluding adding PHYP terminus handle to PHYP’s entity
            // association PDR to avoid deletion of entityAssociation PDRs after
            // a refreshEntireRepo change event.
            if (isHostUp() || (terminus_handle & 0x8000))
            {
                // Record Handle is 0xFFFFFFFF(max value uint32_t), for merging
                // entity association pdr to bmc range
                pldm_entity_association_pdr_add_from_node(
                    node, repo, &entities, numEntities, true, TERMINUS_HANDLE,
                    0xFFFFFFFF);
            }
            else
            {
                // Record Handle is 0xFFFFFFFF(max value uint32_t), for merging
                // entity association pdr to bmc range
                pldm_entity_association_pdr_add_from_node(
                    node, repo, &entities, numEntities, true, terminus_handle,
                    0xFFFFFFFF);
            }
        }
    }
    free(entities);
}

void HostPDRHandler::sendPDRRepositoryChgEvent(std::vector<uint8_t>&& pdrTypes,
                                               uint8_t eventDataFormat)
{
    std::cerr
        << "Sending the repo change event after merging the PDRs, MCTP_ID:"
        << (unsigned)mctp_eid << std::endl;
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
                if (!isHostUp())
                {
                    for (const auto& [terminusHandle, terminusInfo] : tlPDRInfo)
                    {
                        if (std::get<0>(terminusInfo) == terminusID &&
                            std::get<1>(terminusInfo) == mctp_eid &&
                            std::get<2>(terminusInfo) &&
                            record->terminus_handle == terminusHandle)
                        {
                            // send record handles of that terminus only.
                            changeEntries[0].push_back(
                                pldm_pdr_get_record_handle(repo, record));
                        }
                    }
                }
                else
                {
                    // When host is up, Reset Reload case
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
        std::cerr
            << "Failed to encode_pldm_pdr_repository_chg_event_data, rc = "
            << rc << std::endl;
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
        std::cerr << "Failed to encode_platform_event_message_req, rc = " << rc
                  << std::endl;
        return;
    }

    auto platformEventMessageResponseHandler =
        [](mctp_eid_t /*eid*/, const pldm_msg* response, size_t respMsgLen) {
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
    uint32_t nextRecordHandle{};
    uint32_t prevRh{};
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
                      << "rc= "
                      << ", NextRecordhandle :" << rc << nextRecordHandle
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
                    std::cerr
                        << "Got a terminus Locator PDR with TID:"
                        << (unsigned)tid
                        << " and Terminus handle:" << terminusHandle
                        << " with Valid bit as:" << (unsigned)tlpdr->validity
                        << std::endl;
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
                    tlPDRInfo.insert_or_assign(
                        tlpdr->terminus_handle,
                        std::make_tuple(tlpdr->tid, tlEid, tlpdr->validity));
                }
                else if (pdrHdr->type == PLDM_STATE_SENSOR_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_state_sensor_pdr>(pdr);
                    updateContanierId<pldm_state_sensor_pdr>(entityTree, pdr);
                    stateSensorPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_PDR_FRU_RECORD_SET)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_pdr_fru_record_set>(pdr);
                    updateContanierId<pldm_pdr_fru_record_set>(entityTree, pdr);
                    fruRecordSetPDRs.emplace_back(pdr);
                }
                else if (pdrHdr->type == PLDM_STATE_EFFECTER_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_state_effecter_pdr>(pdr);
                    updateContanierId<pldm_state_effecter_pdr>(entityTree, pdr);
                }
                else if (pdrHdr->type == PLDM_NUMERIC_EFFECTER_PDR)
                {
                    pdrTerminusHandle =
                        extractTerminusHandle<pldm_numeric_effecter_value_pdr>(
                            pdr);
                    updateContanierId<pldm_numeric_effecter_value_pdr>(
                        entityTree, pdr);
                }

                // if the TLPDR is invalid update the repo accordingly
                if (!tlValid)
                {
                    pldm_pdr_update_TL_pdr(repo, terminusHandle, tid, tlEid,
                                           tlValid);

                    if (!isHostUp())
                    {
                        // since HB is sending down the TL PDR in the beginning
                        // of the PDR exchange, do not continue PDR exchange
                        // when the TL PDR is invalid.
                        nextRecordHandle = 0;
                    }
                }
                else
                {
                    if ((isHostPdrModified == true) || !(modifiedCounter == 0))
                    {
                        bool recFound =
                            pldm_pdr_find_prev_record_handle(repo, rh, &prevRh);

                        if (recFound)
                        {
                            // pldm_delete_by_record_handle to delete
                            // the effecter from the repo using record handle.
                            pldm_delete_by_record_handle(repo, rh, true);

                            // call pldm_pdr_add_after_prev_record to add the
                            // record into the repo from where it was deleted
                            pldm_pdr_add_after_prev_record(
                                repo, pdr.data(), respCount, rh, true, prevRh,
                                pdrTerminusHandle);

                            if ((pdrHdr->type == PLDM_STATE_EFFECTER_PDR) &&
                                (oemPlatformHandler != nullptr))
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
                                            sizeof(
                                                state_effecter_possible_states) +
                                            state->possible_states_size - 1;
                                    }
                                }
                            }
                            modifiedCounter--;
                        }
                    }
                    // We need to look for an optimal solution for this, we are
                    // unexpectedly entering this path when we receive multiple
                    // modified PDR repo change events
                    else if ((isHostPdrModified != true) &&
                             (modifiedCounter == 0))
                    {
                        bool recFound =
                            pldm_pdr_find_prev_record_handle(repo, rh, &prevRh);
                        if (recFound)
                        {
                            pldm_delete_by_record_handle(repo, rh, true);

                            pldm_pdr_add_after_prev_record(
                                repo, pdr.data(), respCount, rh, true, prevRh,
                                pdrTerminusHandle);
                        }
                        else
                        {
                            pldm_pdr_add(repo, pdr.data(), respCount, rh, true,
                                         pdrTerminusHandle);
                        }
                    }
                }
            }
        }
    }
    if (!nextRecordHandle)
    {
        pldm_pdr_record* firstRecord = repo->first;
        pldm_pdr_record* lastRecord = repo->last;
        std::cerr << "First Record in the repo after PDR exchange is: "
                  << firstRecord->record_handle << std::endl;
        std::cerr << "Last Record in the repo after PDR exchange is:"
                  << lastRecord->record_handle << std::endl;

        pldm::hostbmc::utils::updateEntityAssociation(
            entityAssociations, entityTree, objPathMap, oemPlatformHandler);

        pldm::serialize::Serialize::getSerialize().setObjectPathMaps(
            objPathMap);

        if (oemPlatformHandler != nullptr)
        {
            pldm::hostbmc::utils::setCoreCount(entityAssociations);
        }

        /*received last record*/
        this->parseStateSensorPDRs();
        this->createDbusObjects();
        if (isHostUp())
        {
            std::cout << "Host is UP & Completed the PDR Exchange with host\n";
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
        std::cerr << "GetPLDMVersion encode failure. PLDM error code = "
                  << std::hex << std::showbase << rc << "\n";
        instanceIdDb.free(mctp_eid, instanceId);
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

void HostPDRHandler::setHostSensorState()
{
    sensorIndex = stateSensorPDRs.begin();
    _setHostSensorState();
}

void HostPDRHandler::_setHostSensorState()
{
    if (isHostOff)
    {
        std::cerr
            << "set host state sensor begin : Host is off, stopped sending sensor state commands\n";
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
            std::cerr << "Failed to get State sensor PDR" << std::endl;
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.SetHostSensorState.GetStateSensorPDRFail",
                pldm::PelSeverity::ERROR);
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

                auto instanceId = instanceIdDb.next(mctp_eid);
                std::vector<uint8_t> requestMsg(
                    sizeof(pldm_msg_hdr) +
                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);
                auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
                auto rc = encode_get_state_sensor_readings_req(
                    instanceId, sensorId, sensorRearm, 0, request);

                if (rc != PLDM_SUCCESS)
                {
                    instanceIdDb.free(mctp_eid, instanceId);
                    std::cerr << "Failed to "
                                 "encode_get_state_sensor_readings_req, rc = "
                              << rc << " SensorId=" << sensorId << std::endl;
                    pldm::utils::reportError(
                        "xyz.openbmc_project.PLDM.Error.SetHostSensorState.EncodeStateSensorFail",
                        pldm::PelSeverity::ERROR);
                    return;
                }

                auto getStateSensorReadingRespHandler =
                    [=, this](mctp_eid_t /*eid*/, const pldm_msg* response,
                              size_t respMsgLen) {
                    if (response == nullptr || !respMsgLen)
                    {
                        std::cerr
                            << "Failed to receive response for "
                               "getStateSensorReading command for sensor id="
                            << sensorId << std::endl;
                        if (this->isHostOff)
                        {
                            std::cerr
                                << "set host state sensor : Host is off, stopped sending sensor state commands\n";
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
                        std::cerr
                            << "Failed to "
                               "decode_get_state_sensor_readings_resp, rc = "
                            << rc
                            << " cc=" << static_cast<unsigned>(completionCode)
                            << " SensorId=" << sensorId << std::endl;
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
                                continue;
                            }
                        }

                        if (sensorOffset > compositeSensorStates.size())
                        {
                            std::cerr
                                << " Error Invalid data, Invalid sensor offset,"
                                << " SensorId=" << sensorId << std::endl;
                            return;
                        }

                        const auto& possibleStates =
                            compositeSensorStates[sensorOffset];
                        if (possibleStates.find(eventState) ==
                            possibleStates.end())
                        {
                            std::cerr
                                << " Error invalid_data, Invalid event state,"
                                << " SensorId=" << sensorId << std::endl;
                            return;
                        }
                        const auto& [containerId, entityType,
                                     entityInstance] = entityInfo;
                        auto stateSetId = stateSetIds[sensorOffset];
                        pldm::responder::events::StateSensorEntry
                            stateSensorEntry{containerId,    entityType,
                                             entityInstance, sensorOffset,
                                             false,          stateSetId};
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
                    std::cerr << " Failed to send request to get State sensor "
                                 "reading on Host,"
                              << " SensorId=" << sensorId << std::endl;
                }
            }
        }
    }
}

void HostPDRHandler::getFRURecordTableMetadataByHost()
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
        std::cerr << "Failed to encode_get_fru_record_table_metadata_req, rc = "
                  << rc << std::endl;
        return;
    }

    auto getFruRecordTableMetadataResponseHandler =
        [this](mctp_eid_t /*eid*/, const pldm_msg* response,
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
        this->getFRURecordTableByHost(total);
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

void HostPDRHandler::getFRURecordTableByHost(uint16_t& total_table_records)
{
    fruRecordData.clear();

    if (!total_table_records)
    {
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
        std::cerr << "Failed to encode_get_fru_record_table_req, rc = " << rc
                  << std::endl;
        return;
    }

    auto getFruRecordTableResponseHandler =
        [total_table_records, this](
            mctp_eid_t /*eid*/, const pldm_msg* response, size_t respMsgLen) {
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
        std::vector<uint8_t> fru_record_table_data(respMsgLen);
        auto responsePtr = reinterpret_cast<const struct pldm_msg*>(response);
        auto rc = decode_get_fru_record_table_resp(
            responsePtr, respMsgLen, &cc, &next_data_transfer_handle,
            &transfer_flag, fru_record_table_data.data(),
            &fru_record_table_length);

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

        this->setLocationCode(fruRecordData);
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
    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_GET_STATE_SENSOR_READINGS_REQ_BYTES);

    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    bitfield8_t bf;
    bf.byte = 0;
    auto rc = encode_get_state_sensor_readings_req(instanceId, sensorId, bf, 0,
                                                   request);
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        std::cerr << "Failed to encode_get_state_sensor_readings_req, rc = "
                  << rc << std::endl;
        return;
    }

    auto getStateSensorReadingsResponseHandler =
        [this, path, type, instance, containerId, stateSetId, mctpEid,
         sensorId](mctp_eid_t /*eid*/, const pldm_msg* response,
                   size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            std::cerr
                << "Failed to receive response for get_state_sensor_readings command, sensor id : "
                << sensorId << std::endl;
            // even when for some reason , if we fail to get a response
            // to one sensor, try all the dbus objects

            if (this->isHostOff)
            {
                std::cerr
                    << "Host is off, stopped sending sensor states command\n";
                return;
            }

            ++sensorMapIndex;
            if (sensorMapIndex == sensorMap.end())
            {
                // std::cerr << "sensor map completed\n";
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
            std::cerr << "Faile to decode get state sensor readings resp, "
                         "Message Error: "
                      << "rc=" << rc << ",cc=" << (int)cc << std::endl;
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
                path, state == PLDM_OPERATIONAL_NORMAL, getParentChassis(path));
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
            // std::cerr << "sensor map completed\n";
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
        std::cerr << "Failed to get the State Sensor Readings request\n";
    }

    return;
}

uint16_t HostPDRHandler::getRSI(const pldm_entity& entity)
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

void HostPDRHandler::setLocationCode(
    const std::vector<responder::pdr_utils::FruRecordDataFormat>& fruRecordData)
{
    for (auto& entity : objPathMap)
    {
        pldm_entity node = pldm_entity_extract(entity.second);
        auto fruRSI = getRSI(node);

        for (auto& data : fruRecordData)
        {
            if (fruRSI != data.fruRSI)
            {
                continue;
            }

#ifdef OEM_IBM
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
#endif
            else
            {
                for (auto& tlv : data.fruTLV)
                {
                    if (tlv.fruFieldType == PLDM_FRU_FIELD_TYPE_VERSION &&
                        node.entity_type == PLDM_ENTITY_SYSTEM_CHASSIS)
                    {
                        std::cout << "Refreshing the mex firmware version : "
                                  << std::string(reinterpret_cast<const char*>(
                                                     tlv.fruFieldValue.data()),
                                                 tlv.fruFieldLen)
                                  << std::endl;
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
        pldm_entity node = pldm_entity_extract(objMapIndex->second);

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
    std::cerr << "Refreshing dbus hosted by pldm Started \n";

    objMapIndex = objPathMap.begin();

    sensorMapIndex = sensorMap.begin();

    for (const auto& entity : objPathMap)
    {
        pldm_entity node = pldm_entity_extract(entity.second);
        // update the Present Property
        setPresentPropertyStatus(entity.first);

        // Implement & update the Availability to true
        setAvailabilityState(entity.first);

        switch (node.entity_type)
        {
            case 32903:
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
                CustomDBus::getCustomDBus().implementPanelInterface(
                    entity.first);
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
                CustomDBus::getCustomDBus().setlinkreset(
                    entity.first, false, hostEffecterParser, mctp_eid);
                break;
            case PLDM_ENTITY_CONNECTOR:
                CustomDBus::getCustomDBus().implementConnecterInterface(
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
            case 32954:
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
    this->setFRUDynamicAssociations();
    getFRURecordTableMetadataByHost();

    // update xyz.openbmc_project.State.Decorator.OperationalStatus
    setOperationStatus();
    std::cerr << "Refreshing dbus hosted by pldm Completed \n";
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

        std::cerr << "Deleting the dbus objects of type : " << (unsigned)type
                  << std::endl;
        for (const auto& [path, entites] : savedObjs.at(type))
        {
            if (type !=
                (PLDM_ENTITY_PROC | 0x8000)) // other than CPU core object
            {
                std::cout << "Erasing Dbus Path from ObjectMap " << path
                          << std::endl;
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
    for (const auto& [leftPath, leftElement] : objPathMap)
    {
        // for each path, compare it with rest of the paths in the
        // map
        pldm_entity leftEntity = pldm_entity_extract(leftElement);
        uint16_t leftEntityType = leftEntity.entity_type;
        for (const auto& [rightPath, rightElement] : objPathMap)
        {
            pldm_entity rightEntity = pldm_entity_extract(rightElement);
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

void HostPDRHandler::setRecordPresent(uint32_t recordHandle)
{
    pldm_entity recordEntity = pldm_get_entity_from_record_handle(repo,
                                                                  recordHandle);
    for (const auto& [path, entity_node] : objPathMap)
    {
        pldm_entity dbusEntity = pldm_entity_extract(entity_node);
        if (dbusEntity.entity_type == recordEntity.entity_type &&
            dbusEntity.entity_instance_num ==
                recordEntity.entity_instance_num &&
            dbusEntity.entity_container_id == recordEntity.entity_container_id)
        {
            std::cerr << "Removing Host FRU "
                      << "[ " << path << " ]  with entityid [ "
                      << recordEntity.entity_type << ","
                      << recordEntity.entity_instance_num << ","
                      << recordEntity.entity_container_id << "]" << std::endl;
            // if the record has the same entity id, mark that dbus object as
            // not present
            CustomDBus::getCustomDBus().updateItemPresentStatus(path, false);
            CustomDBus::getCustomDBus().setOperationalStatus(
                path, false, getParentChassis(path));
            return;
        }
    }
}

void HostPDRHandler::deletePDRFromRepo(PDRRecordHandles&& recordHandles)
{
    for (auto& recordHandle : recordHandles)
    {
        std::cerr << "Record handle deleted: " << recordHandle << std::endl;
        this->setRecordPresent(recordHandle);
        pldm_delete_by_record_handle(repo, recordHandle, true);
    }
}

void HostPDRHandler::updateObjectPathMaps(const std::string& path,
                                          pldm_entity_node* node)
{
    objPathMap[path] = node;
}

} // namespace pldm
