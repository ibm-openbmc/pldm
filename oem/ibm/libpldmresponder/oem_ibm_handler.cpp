#include "oem_ibm_handler.hpp"

#include "libpldm/entity.h"
#include "libpldm/pldm.h"

#include "collect_slot_vpd.hpp"
#include "file_io_type_lid.hpp"
#include "libpldmresponder/file_io.hpp"
#include "libpldmresponder/pdr_utils.hpp"

#include <regex>

using namespace pldm::pdr;
using namespace pldm::utils;

namespace pldm
{
namespace responder
{
namespace oem_ibm_platform
{
std::vector<InstanceInfo> generateProcAndDcmIDs()
{
    std::vector<InstanceInfo> dcmProcInfo;
    std::vector<std::string> procObjectPaths;
    procObjectPaths = getProcObjectPaths();

    for (const auto& entity_path : procObjectPaths)
    {
        if (entity_path.rfind('/') != std::string::npos)
        {
            char pId = entity_path.back();
            auto procId = pId - 48;
            char id = entity_path.at(61);
            auto dcmId = id - 48;

            dcmProcInfo.emplace_back(InstanceInfo{static_cast<uint8_t>(procId),
                                                  static_cast<uint8_t>(dcmId)});
        }
    }
    return dcmProcInfo;
}

int pldm::responder::oem_ibm_platform::Handler::
    oemSetNumericEffecterValueHandler(
        uint16_t entityType, uint16_t entityInstance,
        uint16_t effecterSemanticId, uint8_t effecterDataSize,
        uint8_t* effecterValue, real32_t effecterOffset,
        real32_t effecterResolution, uint16_t effecterId)
{
    int rc = PLDM_SUCCESS;

    if (entityType == PLDM_ENTITY_PROC &&
        effecterSemanticId == PLDM_OEM_IBM_SBE_SEMANTIC_ID &&
        effecterDataSize == PLDM_EFFECTER_DATA_SIZE_UINT32)
    {
        uint32_t currentValue =
            *(reinterpret_cast<uint32_t*>(&effecterValue[0]));
        auto rawValue = static_cast<uint32_t>(
            round(currentValue - effecterOffset) / effecterResolution);
        pldm::utils::PropertyValue value;
        value = rawValue;

        for (auto& [key, value] : instanceMap)
        {
            if (key == effecterId)
            {
                entityInstance = (value.dcmId * 2) +
                                 value.procId; // failingUintId
            }
        }
        rc = setNumericEffecter(entityInstance, value);
    }
    return rc;
}

int pldm::responder::oem_ibm_platform::Handler::
    getOemStateSensorReadingsHandler(
        EntityType entityType, EntityInstance entityInstance,
        ContainerID containerId, StateSetId stateSetId,
        CompositeCount compSensorCnt, uint16_t /*sensorId*/,
        std::vector<get_sensor_state_field>& stateField)
{
    auto& entityAssociationMap = getAssociateEntityMap();
    int rc = PLDM_SUCCESS;
    stateField.clear();

    for (size_t i = 0; i < compSensorCnt; i++)
    {
        uint8_t sensorOpState{};
        if (entityType == PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE &&
            stateSetId == PLDM_OEM_IBM_BOOT_STATE)
        {
            sensorOpState = fetchBootSide(entityInstance, codeUpdate);
            stateField.push_back({PLDM_SENSOR_ENABLED, PLDM_SENSOR_UNKNOWN,
                                  PLDM_SENSOR_UNKNOWN, sensorOpState});
        }
        else if (entityType == PLDM_ENTITY_SLOT &&
                 stateSetId == PLDM_OEM_IBM_SLOT_ENABLE_SENSOR_STATE)
        {
            for (const auto& [key, value] : entityAssociationMap)
            {
                if (value.entity_type == entityType &&
                    value.entity_instance_num == entityInstance &&
                    value.entity_container_id == containerId)
                {
                    sensorOpState = slotHandler->fetchSlotSensorState(key);
                    break;
                }
            }
        }
        else
        {
            rc = PLDM_PLATFORM_INVALID_STATE_VALUE;
            break;
        }
        stateField.push_back({PLDM_SENSOR_ENABLED, PLDM_SENSOR_UNKNOWN,
                              PLDM_SENSOR_UNKNOWN, sensorOpState});
    }
    return rc;
}

int pldm::responder::oem_ibm_platform::Handler::
    oemSetStateEffecterStatesHandler(
        uint16_t entityType, uint16_t /*entityInstance*/, uint16_t stateSetId,
        uint8_t compEffecterCnt,
        std::vector<set_effecter_state_field>& stateField, uint16_t effecterId)
{
    int rc = PLDM_SUCCESS;
    auto& entityAssociationMap = getAssociateEntityMap();

    for (uint8_t currState = 0; currState < compEffecterCnt; ++currState)
    {
        if (stateField[currState].set_request == PLDM_REQUEST_SET)
        {
            if (entityType == PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE &&
                stateSetId == PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE)
            {
                if (stateField[currState].effecter_state ==
                    uint8_t(CodeUpdateState::START))
                {
                    std::cout << "Received Start Update Request From PHYP\n";
                    codeUpdate->setCodeUpdateProgress(true);
                    startUpdateEvent =
                        std::make_unique<sdeventplus::source::Defer>(
                            event,
                            std::bind(std::mem_fn(&oem_ibm_platform::Handler::
                                                      _processStartUpdate),
                                      this, std::placeholders::_1));
                }
                else if (stateField[currState].effecter_state ==
                         uint8_t(CodeUpdateState::END))
                {
                    std::cout << "Received End Update Request From PHYP\n";
                    rc = PLDM_SUCCESS;
                    assembleImageEvent = std::make_unique<
                        sdeventplus::source::Defer>(
                        event,
                        std::bind(
                            std::mem_fn(
                                &oem_ibm_platform::Handler::_processEndUpdate),
                            this, std::placeholders::_1));

                    // sendCodeUpdateEvent(effecterId, END, START);
                }
                else if (stateField[currState].effecter_state ==
                         uint8_t(CodeUpdateState::ABORT))
                {
                    std::cout << "Received Abort Update Request From PHYP\n";
                    codeUpdate->setCodeUpdateProgress(false);
                    codeUpdate->clearDirPath(LID_STAGING_DIR);
                    auto sensorId = codeUpdate->getFirmwareUpdateSensor();
                    sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                                         uint8_t(CodeUpdateState::ABORT),
                                         uint8_t(CodeUpdateState::START));
                    // sendCodeUpdateEvent(effecterId, ABORT, END);
                }
                else if (stateField[currState].effecter_state ==
                         uint8_t(CodeUpdateState::ACCEPT))
                {
                    auto sensorId = codeUpdate->getFirmwareUpdateSensor();
                    sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                                         uint8_t(CodeUpdateState::ACCEPT),
                                         uint8_t(CodeUpdateState::END));
                    // TODO Set new Dbus property provided by code update app
                    // sendCodeUpdateEvent(effecterId, ACCEPT, END);
                }
                else if (stateField[currState].effecter_state ==
                         uint8_t(CodeUpdateState::REJECT))
                {
                    auto sensorId = codeUpdate->getFirmwareUpdateSensor();
                    sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                                         uint8_t(CodeUpdateState::REJECT),
                                         uint8_t(CodeUpdateState::END));
                    // TODO Set new Dbus property provided by code update app
                    // sendCodeUpdateEvent(effecterId, REJECT, END);
                }
            }
            else if (entityType == PLDM_ENTITY_SYSTEM_CHASSIS &&
                     stateSetId == PLDM_OEM_IBM_SYSTEM_POWER_STATE)
            {
                if (stateField[currState].effecter_state == POWER_CYCLE_HARD)
                {
                    std::cout << "Got a Deep IPL request" << std::endl;
                    systemRebootEvent =
                        std::make_unique<sdeventplus::source::Defer>(
                            event,
                            std::bind(std::mem_fn(&oem_ibm_platform::Handler::
                                                      _processSystemReboot),
                                      this, std::placeholders::_1));
                }
            }
            else if (stateSetId == PLDM_OEM_IBM_SLOT_ENABLE_EFFECTER_STATE)
            {
                slotHandler->enableSlot(effecterId, entityAssociationMap,
                                        stateField[currState].effecter_state);
            }
            else if (entityType == PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE &&
                     stateSetId == PLDM_OEM_IBM_BOOT_SIDE_RENAME)
            {
                if (stateField[currState].effecter_state ==
                    PLDM_BOOT_SIDE_HAS_BEEN_RENAMED)
                {
                    codeUpdate->processRenameEvent();
                }
            }
            else if (entityType == PLDM_OEM_IBM_ENTITY_REAL_SAI &&
                     stateSetId == PLDM_STATE_SET_OPERATIONAL_FAULT_STATUS)
            {
                turnOffRealSAIEffecter();
            }
            else if (entityType == PLDM_OEM_IBM_CHASSIS_POWER_CONTROLLER &&
                     stateSetId == PLDM_STATE_SET_SYSTEM_POWER_STATE)
            {
                if (stateField[currState].effecter_state ==
                    PLDM_STATE_SET_SYS_POWER_CYCLE_OFF_SOFT_GRACEFUL)
                {
                    processPowerCycleOffSoftGraceful();
                }
                else if (stateField[currState].effecter_state ==
                         PLDM_STATE_SET_SYS_POWER_STATE_OFF_SOFT_GRACEFUL)
                {
                    processPowerOffSoftGraceful();
                }
                else if (stateField[currState].effecter_state ==
                         PLDM_STATE_SET_SYS_POWER_STATE_OFF_HARD_GRACEFUL)
                {
                    processPowerOffHardGraceful();
                }
            }
            else
            {
                rc = PLDM_PLATFORM_SET_EFFECTER_UNSUPPORTED_SENSORSTATE;
            }
        }
        if (rc != PLDM_SUCCESS)
        {
            break;
        }
    }
    return rc;
}

void buildAllCodeUpdateEffecterPDR(oem_ibm_platform::Handler* platformHandler,
                                   uint16_t entityType, uint16_t entityInstance,
                                   uint16_t stateSetID, pdr_utils::Repo& repo)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_effecter_pdr) +
              sizeof(state_effecter_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_effecter_pdr* pdr =
        reinterpret_cast<pldm_state_effecter_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_EFFECTER_ID << std::endl;
        return;
    }
    pdr->hdr.record_handle = 0;
    pdr->hdr.version = 1;
    pdr->hdr.type = PLDM_STATE_EFFECTER_PDR;
    pdr->hdr.record_change_num = 0;
    pdr->hdr.length = sizeof(pldm_state_effecter_pdr) - sizeof(pldm_pdr_hdr);
    pdr->terminus_handle = TERMINUS_HANDLE;
    pdr->effecter_id = platformHandler->getNextEffecterId();
    pdr->entity_type = entityType;
    pdr->entity_instance = entityInstance;
    pdr->container_id = 1;
    pdr->effecter_semantic_id = 0;
    pdr->effecter_init = PLDM_NO_INIT;
    pdr->has_description_pdr = false;
    pdr->composite_effecter_count = 1;

    auto* possibleStatesPtr = pdr->possible_states;
    auto possibleStates =
        reinterpret_cast<state_effecter_possible_states*>(possibleStatesPtr);
    possibleStates->state_set_id = stateSetID;
    possibleStates->possible_states_size = 2;
    auto state =
        reinterpret_cast<state_effecter_possible_states*>(possibleStates);
    if (stateSetID == PLDM_OEM_IBM_BOOT_SIDE_RENAME)
        state->states[0].byte = 6;
    else if (stateSetID == PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE)
        state->states[0].byte = 126;
    else if (stateSetID == PLDM_OEM_IBM_SYSTEM_POWER_STATE)
        state->states[0].byte = 2;
    pldm::responder::pdr_utils::PdrEntry pdrEntry{};
    pdrEntry.data = entry.data();
    pdrEntry.size = pdrSize;
    repo.addRecord(pdrEntry);
}

void buildAllSlotEnabeEffecterPDR(oem_ibm_platform::Handler* platformHandler,
                                  pdr_utils::Repo& repo,
                                  const std::vector<std::string>& slotobjpaths)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_effecter_pdr) +
              sizeof(state_effecter_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_effecter_pdr* pdr =
        reinterpret_cast<pldm_state_effecter_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_EFFECTER_ID << std::endl;
        return;
    }

    auto& associatedEntityMap = platformHandler->getAssociateEntityMap();
    for (const auto& entity_path : slotobjpaths)
    {
        pdr->hdr.record_handle = 0;
        pdr->hdr.version = 1;
        pdr->hdr.type = PLDM_STATE_EFFECTER_PDR;
        pdr->hdr.record_change_num = 0;
        pdr->hdr.length = sizeof(pldm_state_effecter_pdr) -
                          sizeof(pldm_pdr_hdr);
        pdr->terminus_handle = TERMINUS_HANDLE;
        pdr->effecter_id = platformHandler->getNextEffecterId();

        if (entity_path != "" &&
            associatedEntityMap.find(entity_path) != associatedEntityMap.end())
        {
            pdr->entity_type = associatedEntityMap.at(entity_path).entity_type;
            pdr->entity_instance =
                associatedEntityMap.at(entity_path).entity_instance_num;
            pdr->container_id =
                associatedEntityMap.at(entity_path).entity_container_id;
            platformHandler->effecterIdToDbusMap[pdr->effecter_id] =
                entity_path;
        }
        else
        {
            // the slots are not present, dont create the PDR
            continue;
        }
        pdr->effecter_semantic_id = 0;
        pdr->effecter_init = PLDM_NO_INIT;
        pdr->has_description_pdr = false;
        pdr->composite_effecter_count = 1;

        auto* possibleStatesPtr = pdr->possible_states;
        auto possibleStates = reinterpret_cast<state_effecter_possible_states*>(
            possibleStatesPtr);
        possibleStates->state_set_id = PLDM_OEM_IBM_SLOT_ENABLE_EFFECTER_STATE;
        possibleStates->possible_states_size = 2;
        auto state =
            reinterpret_cast<state_effecter_possible_states*>(possibleStates);
        state->states[0].byte = 14;
        pldm::responder::pdr_utils::PdrEntry pdrEntry{};
        pdrEntry.data = entry.data();
        pdrEntry.size = pdrSize;
        repo.addRecord(pdrEntry);
    }
}

void buildAllCodeUpdateSensorPDR(oem_ibm_platform::Handler* platformHandler,
                                 uint16_t entityType, uint16_t entityInstance,
                                 uint16_t stateSetID, pdr_utils::Repo& repo)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_sensor_pdr) +
              sizeof(state_sensor_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_sensor_pdr* pdr =
        reinterpret_cast<pldm_state_sensor_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_SENSOR_ID << std::endl;
        return;
    }
    pdr->hdr.record_handle = 0;
    pdr->hdr.version = 1;
    pdr->hdr.type = PLDM_STATE_SENSOR_PDR;
    pdr->hdr.record_change_num = 0;
    pdr->hdr.length = sizeof(pldm_state_sensor_pdr) - sizeof(pldm_pdr_hdr);
    pdr->terminus_handle = TERMINUS_HANDLE;
    pdr->sensor_id = platformHandler->getNextSensorId();
    pdr->entity_type = entityType;
    pdr->entity_instance = entityInstance;
    pdr->container_id = 1;
    pdr->sensor_init = PLDM_NO_INIT;
    pdr->sensor_auxiliary_names_pdr = false;
    pdr->composite_sensor_count = 1;

    auto* possibleStatesPtr = pdr->possible_states;
    auto possibleStates =
        reinterpret_cast<state_sensor_possible_states*>(possibleStatesPtr);
    possibleStates->state_set_id = stateSetID;
    possibleStates->possible_states_size = 2;
    auto state =
        reinterpret_cast<state_sensor_possible_states*>(possibleStates);
    if ((stateSetID == PLDM_OEM_IBM_VERIFICATION_STATE) ||
        (stateSetID == PLDM_OEM_IBM_BOOT_SIDE_RENAME))
        state->states[0].byte = 6;
    else if (stateSetID == PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE)
        state->states[0].byte = 126;
    pldm::responder::pdr_utils::PdrEntry pdrEntry{};
    pdrEntry.data = entry.data();
    pdrEntry.size = pdrSize;
    repo.addRecord(pdrEntry);
}

void buildAllSlotEnableSensorPDR(oem_ibm_platform::Handler* platformHandler,
                                 pdr_utils::Repo& repo,
                                 const std::vector<std::string>& slotobjpaths)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_sensor_pdr) +
              sizeof(state_sensor_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_sensor_pdr* pdr =
        reinterpret_cast<pldm_state_sensor_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_SENSOR_ID << std::endl;
        return;
    }
    auto& associatedEntityMap = platformHandler->getAssociateEntityMap();
    for (const auto& entity_path : slotobjpaths)
    {
        pdr->hdr.record_handle = 0;
        pdr->hdr.version = 1;
        pdr->hdr.type = PLDM_STATE_SENSOR_PDR;
        pdr->hdr.record_change_num = 0;
        pdr->hdr.length = sizeof(pldm_state_sensor_pdr) - sizeof(pldm_pdr_hdr);
        pdr->terminus_handle = TERMINUS_HANDLE;
        pdr->sensor_id = platformHandler->getNextSensorId();
        if (entity_path != "" &&
            associatedEntityMap.find(entity_path) != associatedEntityMap.end())
        {
            pdr->entity_type = associatedEntityMap.at(entity_path).entity_type;
            pdr->entity_instance =
                associatedEntityMap.at(entity_path).entity_instance_num;
            pdr->container_id =
                associatedEntityMap.at(entity_path).entity_container_id;
        }
        else
        {
            // the slots are not present, dont create the PDR
            continue;
        }

        pdr->sensor_init = PLDM_NO_INIT;
        pdr->sensor_auxiliary_names_pdr = false;
        pdr->composite_sensor_count = 1;

        auto* possibleStatesPtr = pdr->possible_states;
        auto possibleStates =
            reinterpret_cast<state_sensor_possible_states*>(possibleStatesPtr);
        possibleStates->state_set_id = PLDM_OEM_IBM_SLOT_ENABLE_SENSOR_STATE;
        possibleStates->possible_states_size = 1;
        auto state =
            reinterpret_cast<state_sensor_possible_states*>(possibleStates);
        state->states[0].byte = 15;
        pldm::responder::pdr_utils::PdrEntry pdrEntry{};
        pdrEntry.data = entry.data();
        pdrEntry.size = pdrSize;
        repo.addRecord(pdrEntry);
    }
}

void buildAllNumericEffecterPDR(oem_ibm_platform::Handler* platformHandler,
                                uint16_t entityType, uint16_t entityInstance,
                                uint16_t effecterSemanticId,
                                pdr_utils::Repo& repo,
                                HostEffecterInstanceMap& instanceMap)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_numeric_effecter_value_pdr);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_numeric_effecter_value_pdr* pdr =
        reinterpret_cast<pldm_numeric_effecter_value_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_EFFECTER_ID << std::endl;
        return;
    }

    std::vector<InstanceInfo> info;
    info = generateProcAndDcmIDs();

    for (auto procDcmInfo : info)
    {
        pdr->hdr.record_handle = 0;
        pdr->hdr.version = 1;
        pdr->hdr.type = PLDM_NUMERIC_EFFECTER_PDR;
        pdr->hdr.record_change_num = 0;
        pdr->hdr.length = sizeof(pldm_numeric_effecter_value_pdr) -
                          sizeof(pldm_pdr_hdr);
        pdr->terminus_handle = TERMINUS_HANDLE;
        pdr->effecter_id = platformHandler->getNextEffecterId();

        uint16_t effecterId = pdr->effecter_id;

        pdr->entity_type = entityType;

        instanceMap.emplace(effecterId, procDcmInfo);

        entityInstance = procDcmInfo.procId;
        ;

        pdr->entity_instance = entityInstance;
        pdr->container_id = 3;
        pdr->effecter_semantic_id = effecterSemanticId;
        pdr->effecter_init = PLDM_NO_INIT;
        pdr->effecter_auxiliary_names = false;
        pdr->base_unit = 0;
        pdr->unit_modifier = 0;
        pdr->rate_unit = 0;
        pdr->base_oem_unit_handle = 0;
        pdr->aux_unit = 0;
        pdr->aux_unit_modifier = 0;
        pdr->aux_oem_unit_handle = 0;
        pdr->aux_rate_unit = 0;
        pdr->is_linear = true;
        pdr->effecter_data_size = PLDM_EFFECTER_DATA_SIZE_UINT32;
        pdr->resolution = 1.00;
        pdr->offset = 0.00;
        pdr->accuracy = 0;
        pdr->plus_tolerance = 0;
        pdr->minus_tolerance = 0;
        pdr->state_transition_interval = 0.00;
        pdr->transition_interval = 0.00;
        pdr->max_settable.value_u32 = 0xFFFFFFFF;
        pdr->min_settable.value_u32 = 0x0;
        pdr->range_field_format = 0;
        pdr->range_field_support.byte = 0;
        pdr->nominal_value.value_u32 = 0;
        pdr->normal_max.value_u32 = 0;
        pdr->normal_min.value_u32 = 0;
        pdr->rated_max.value_u32 = 0;
        pdr->rated_min.value_u32 = 0;

        pldm::responder::pdr_utils::PdrEntry pdrEntry{};
        pdrEntry.data = entry.data();
        pdrEntry.size = pdrSize;
        repo.addRecord(pdrEntry);
    }
}

void buildAllSystemPowerStateEffecterPDR(
    oem_ibm_platform::Handler* platformHandler, uint16_t entityType,
    uint16_t entityInstance, uint16_t stateSetID, pdr_utils::Repo& repo)
{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_effecter_pdr) +
              sizeof(state_effecter_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_effecter_pdr* pdr =
        reinterpret_cast<pldm_state_effecter_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_EFFECTER_ID << std::endl;
        return;
    }
    pdr->hdr.record_handle = 0;
    pdr->hdr.version = 1;
    pdr->hdr.type = PLDM_STATE_EFFECTER_PDR;
    pdr->hdr.record_change_num = 0;
    pdr->hdr.length = sizeof(pldm_state_effecter_pdr) - sizeof(pldm_pdr_hdr);
    pdr->terminus_handle = TERMINUS_HANDLE;
    pdr->effecter_id = platformHandler->getNextEffecterId();
    pdr->entity_type = entityType;
    pdr->entity_instance = entityInstance;
    pdr->container_id = 1;
    pdr->effecter_semantic_id = 0;
    pdr->effecter_init = PLDM_NO_INIT;
    pdr->has_description_pdr = false;
    pdr->composite_effecter_count = 1;

    auto* possibleStatesPtr = pdr->possible_states;
    auto possibleStates =
        reinterpret_cast<state_effecter_possible_states*>(possibleStatesPtr);
    possibleStates->state_set_id = stateSetID;
    possibleStates->possible_states_size = 2;
    auto state =
        reinterpret_cast<state_effecter_possible_states*>(possibleStates);
    state->states[0].byte = 128;
    state->states[1].byte = 6;
    pldm::responder::pdr_utils::PdrEntry pdrEntry{};
    pdrEntry.data = entry.data();
    pdrEntry.size = pdrSize;
    repo.addRecord(pdrEntry);
}

std::vector<std::string> getslotPaths()
{
    static constexpr auto searchpath = "/xyz/openbmc_project/inventory/system";
    int depth = 0;
    pldm::utils::GetSubTreeResponse response =
        pldm::utils::DBusHandler().getSubtree(
            searchpath, depth, {"xyz.openbmc_project.Inventory.Item.PCIeSlot"});
    std::vector<std::string> slotPaths;
    std::transform(response.begin(), response.end(),
                   std::back_inserter(slotPaths),
                   [](const auto& kv) { return kv.first; });
    return slotPaths;
}

void attachOemEntityToEntityAssociationPDR(
    oem_ibm_platform::Handler* platformHandler,
    pldm_entity_association_tree* bmcEntityTree,
    const std::string& parentEntityPath, pdr_utils::Repo& repo,
    pldm_entity childEntity)
{
    auto& associatedEntityMap = platformHandler->getAssociateEntityMap();
    if (associatedEntityMap.contains(parentEntityPath))
    {
        // Parent is present in the entity association PDR
        pldm_entity parent_entity = associatedEntityMap.at(parentEntityPath);
        auto parent_node = pldm_entity_association_tree_find(
            bmcEntityTree, &parent_entity, false);
        if (!parent_node)
        {
            // parent node not found in the entity association tree,
            // this should not be possible
            std::cerr << "Parent Entity of type " << parent_entity.entity_type
                      << " not found in the BMC Entity Association tree\n";
            return;
        }
        uint32_t bmc_record_handle = 0;
#ifdef OEM_IBM
        auto lastLocalRecord = pldm_pdr_find_last_local_record(repo.getPdr());
        bmc_record_handle = lastLocalRecord->record_handle;
#endif
        pldm_entity_association_tree_add(
            bmcEntityTree, &childEntity, 0xFFFF, parent_node,
            PLDM_ENTITY_ASSOCIAION_PHYSICAL, false, false, 0xFFFF);

        uint8_t bmcEventDataOps = PLDM_INVALID_OP;
        pldm_entity_association_pdr_add_contained_entity(
            repo.getPdr(), childEntity, parent_entity, &bmcEventDataOps, false,
            bmc_record_handle);
    }
}

std::filesystem::path pldm::responder::oem_ibm_platform::Handler::getConfigDir()
{
    if (!systemType.empty())
    {
        return fs::path{systemType};
    }

    namespace fs = std::filesystem;
    static constexpr auto compatibleInterface =
        "xyz.openbmc_project.Configuration.IBMCompatibleSystem";
    static constexpr auto namesProperty = "Names";
    static constexpr auto orgFreeDesktopInterface =
        "org.freedesktop.DBus.Properties";
    static constexpr auto getMethod = "Get";

    static constexpr auto searchpath = "/xyz/openbmc_project/";
    int depth = 0;
    std::vector<std::string> ibmCompatible = {compatibleInterface};
    pldm::utils::GetSubTreeResponse response =
        pldm::utils::DBusHandler().getSubtree(searchpath, depth, ibmCompatible);
    auto& bus = pldm::utils::DBusHandler::getBus();
    std::variant<std::vector<std::string>> value;

    for (const auto& [objectPath, serviceMap] : response)
    {
        try
        {
            auto method = bus.new_method_call(
                serviceMap[0].first.c_str(), objectPath.c_str(),
                orgFreeDesktopInterface, getMethod);
            method.append(ibmCompatible[0].c_str(), namesProperty);
            auto reply = bus.call(
                method, std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT))
                            .count());
            reply.read(value);
            return fs::path{std::get<std::vector<std::string>>(value)[0]};
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error getting Names property , PATH=" << objectPath
                      << " Compatible interface =" << ibmCompatible[0] << "\n";
        }
    }
    return fs::path();
}

void buildAllRealSAIEffecterPDR(oem_ibm_platform::Handler* platformHandler,
                                uint16_t entityType, uint16_t entityInstance,
                                pdr_utils::Repo& repo)

{
    size_t pdrSize = 0;
    pdrSize = sizeof(pldm_state_effecter_pdr) +
              sizeof(state_effecter_possible_states);
    std::vector<uint8_t> entry{};
    entry.resize(pdrSize);
    pldm_state_effecter_pdr* pdr =
        reinterpret_cast<pldm_state_effecter_pdr*>(entry.data());
    if (!pdr)
    {
        std::cerr << "Failed to get record by PDR type, ERROR:"
                  << PLDM_PLATFORM_INVALID_EFFECTER_ID << std::endl;
        return;
    }
    pdr->hdr.record_handle = 0;
    pdr->hdr.version = 1;
    pdr->hdr.type = PLDM_STATE_EFFECTER_PDR;
    pdr->hdr.record_change_num = 0;
    pdr->hdr.length = sizeof(pldm_state_effecter_pdr) - sizeof(pldm_pdr_hdr);
    pdr->terminus_handle = TERMINUS_HANDLE;
    pdr->effecter_id = platformHandler->getNextEffecterId();
    pdr->entity_type = entityType;
    pdr->entity_instance = entityInstance;
    pdr->container_id = 1;
    pdr->effecter_semantic_id = 0;
    pdr->effecter_init = PLDM_NO_INIT;
    pdr->has_description_pdr = false;
    pdr->composite_effecter_count = 1;

    auto* possibleStatesPtr = pdr->possible_states;
    auto possibleStates =
        reinterpret_cast<state_effecter_possible_states*>(possibleStatesPtr);
    possibleStates->state_set_id = 10;
    possibleStates->possible_states_size = 1;
    auto state =
        reinterpret_cast<state_effecter_possible_states*>(possibleStates);
    state->states[0].byte = 2;
    pldm::responder::pdr_utils::PdrEntry pdrEntry{};
    pdrEntry.data = entry.data();
    pdrEntry.size = pdrSize;
    repo.addRecord(pdrEntry);
}

void pldm::responder::oem_ibm_platform::Handler::buildOEMPDR(
    pdr_utils::Repo& repo)
{
    buildAllCodeUpdateEffecterPDR(this, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                                  ENTITY_INSTANCE_0,
                                  PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE, repo);
    buildAllCodeUpdateEffecterPDR(this, PLDM_ENTITY_SYSTEM_CHASSIS,
                                  ENTITY_INSTANCE_1,
                                  PLDM_OEM_IBM_SYSTEM_POWER_STATE, repo);

    buildAllSlotEnabeEffecterPDR(this, repo, getslotPaths());
    buildAllSlotEnableSensorPDR(this, repo, getslotPaths());

    buildAllRealSAIEffecterPDR(this, PLDM_OEM_IBM_ENTITY_REAL_SAI,
                               ENTITY_INSTANCE_1, repo);

    buildAllCodeUpdateEffecterPDR(this, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                                  ENTITY_INSTANCE_0,
                                  PLDM_OEM_IBM_BOOT_SIDE_RENAME, repo);

    buildAllCodeUpdateSensorPDR(this, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                                ENTITY_INSTANCE_0,
                                PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE, repo);
    buildAllCodeUpdateSensorPDR(this, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                                ENTITY_INSTANCE_0,
                                PLDM_OEM_IBM_VERIFICATION_STATE, repo);
    buildAllCodeUpdateSensorPDR(this, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                                ENTITY_INSTANCE_0,
                                PLDM_OEM_IBM_BOOT_SIDE_RENAME, repo);
    buildAllNumericEffecterPDR(this, PLDM_ENTITY_PROC, ENTITY_INSTANCE_0,
                               PLDM_OEM_IBM_SBE_SEMANTIC_ID, repo, instanceMap);
    buildAllSystemPowerStateEffecterPDR(
        this, PLDM_OEM_IBM_CHASSIS_POWER_CONTROLLER, ENTITY_INSTANCE_0,
        PLDM_STATE_SET_SYSTEM_POWER_STATE, repo);

    pldm_entity fwUpEntity = {PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE, 0, 1};
    attachOemEntityToEntityAssociationPDR(
        this, bmcEntityTree, "/xyz/openbmc_project/inventory/system", repo,
        fwUpEntity);
    pldm_entity powerStateEntity = {PLDM_OEM_IBM_CHASSIS_POWER_CONTROLLER, 0,
                                    1};
    attachOemEntityToEntityAssociationPDR(
        this, bmcEntityTree, "/xyz/openbmc_project/inventory/system", repo,
        powerStateEntity);

    auto sensorId = findStateSensorId(
        repo.getPdr(), 0, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
        ENTITY_INSTANCE_0, 1, PLDM_OEM_IBM_VERIFICATION_STATE);
    codeUpdate->setMarkerLidSensor(sensorId);
    sensorId = findStateSensorId(
        repo.getPdr(), 0, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
        ENTITY_INSTANCE_0, 1, PLDM_OEM_IBM_FIRMWARE_UPDATE_STATE);
    codeUpdate->setFirmwareUpdateSensor(sensorId);
    sensorId =
        findStateSensorId(repo.getPdr(), 0, PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE,
                          ENTITY_INSTANCE_0, 1, PLDM_OEM_IBM_BOOT_SIDE_RENAME);
    codeUpdate->setBootSideRenameStateSensor(sensorId);
}

void pldm::responder::oem_ibm_platform::Handler::setPlatformHandler(
    pldm::responder::platform::Handler* handler)
{
    platformHandler = handler;
}

int pldm::responder::oem_ibm_platform::Handler::sendEventToHost(
    std::vector<uint8_t>& requestMsg, uint8_t instanceId)
{
    if (requestMsg.size())
    {
        std::ostringstream tempStream;
        for (int byte : requestMsg)
        {
            tempStream << std::setfill('0') << std::setw(2) << std::hex << byte
                       << " ";
        }
        std::cout << tempStream.str() << std::endl;
    }
    auto oemPlatformEventMessageResponseHandler =
        [](mctp_eid_t /*eid*/, const pldm_msg* response, size_t respMsgLen) {
        uint8_t completionCode{};
        uint8_t status{};
        auto rc = decode_platform_event_message_resp(response, respMsgLen,
                                                     &completionCode, &status);
        if (rc || completionCode)
        {
            std::cerr << "Failed to decode_platform_event_message_resp: "
                      << " for code update event rc=" << rc
                      << ", cc=" << static_cast<unsigned>(completionCode)
                      << std::endl;
        }
    };
    auto rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_PLATFORM_EVENT_MESSAGE,
        std::move(requestMsg),
        std::move(oemPlatformEventMessageResponseHandler));
    if (rc)
    {
        std::cerr << "Failed to send BIOS attribute change event message \n";
    }

    return rc;
}

int encodeEventMsg(uint8_t eventType, const std::vector<uint8_t>& eventDataVec,
                   std::vector<uint8_t>& requestMsg, uint8_t instanceId)
{
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());

    auto rc = encode_platform_event_message_req(
        instanceId, 1 /*formatVersion*/, TERMINUS_ID, eventType,
        eventDataVec.data(), eventDataVec.size(), request,
        eventDataVec.size() + PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES);

    return rc;
}
void pldm::responder::oem_ibm_platform::Handler::setHostEffecterState(
    bool status)
{
    pldm::pdr::EntityType entityType = PLDM_ENTITY_PROC;
    pldm::pdr::StateSetId stateSetId = PLDM_OEM_IBM_SBE_MAINTENANCE_STATE;

    uint8_t tid = TERMINUS_ID;

    auto pdrs = findStateEffecterPDR(tid, entityType, stateSetId, pdrRepo);
    for (auto& pdr : pdrs)
    {
        auto stateEffecterPDR =
            reinterpret_cast<pldm_state_effecter_pdr*>(pdr.data());
        uint16_t effecterId = stateEffecterPDR->effecter_id;
        uint8_t compEffecterCount = stateEffecterPDR->composite_effecter_count;

        std::vector<uint8_t> requestMsg(
            sizeof(pldm_msg_hdr) + sizeof(effecterId) +
                sizeof(compEffecterCount) +
                sizeof(set_effecter_state_field) * compEffecterCount,
            0);

        auto instanceId = instanceIdDb.next(mctp_eid);

        auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
        std::vector<set_effecter_state_field> stateField;
        if (status == true)
        {
            stateField.push_back(
                set_effecter_state_field{PLDM_REQUEST_SET, SBE_DUMP_COMPLETED});
        }
        else
        {
            stateField.push_back(
                set_effecter_state_field{PLDM_REQUEST_SET, SBE_RETRY_REQUIRED});
        }
        auto rc = encode_set_state_effecter_states_req(
            instanceId, effecterId, compEffecterCount, stateField.data(),
            request);
        if (rc != PLDM_SUCCESS)
        {
            std::cerr
                << " Set state effecter state command failure. PLDM error code ="
                << rc << std::endl;
            instanceIdDb.free(mctp_eid, instanceId);
            return;
        }
        auto setStateEffecterStatesRespHandler =
            [=, this](mctp_eid_t /*eid*/, const pldm_msg* response,
                      size_t respMsgLen) {
            if (response == nullptr || !respMsgLen)
            {
                std::cerr << "Failed to receive response for "
                          << "setstateEffecterSates command\n";
                return;
            }
            uint8_t completionCode{};
            auto rc = decode_set_state_effecter_states_resp(
                response, respMsgLen, &completionCode);
            if (rc)
            {
                std::cerr << "Failed to decode setStateEffecterStates response,"
                          << " rc " << rc << "\n";
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.SetHostEffecterFailed",
                    pldm::PelSeverity::ERROR);
            }
            if (completionCode)
            {
                std::cerr << "Failed to set a Host effecter "
                          << ", cc=" << static_cast<unsigned>(completionCode)
                          << "\n";
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.SetHostEffecterFailed",
                    pldm::PelSeverity::ERROR);
            }
        };
        rc = handler->registerRequest(
            mctp_eid, instanceId, PLDM_PLATFORM, PLDM_SET_STATE_EFFECTER_STATES,
            std::move(requestMsg),
            std::move(setStateEffecterStatesRespHandler));
        if (rc)
        {
            std::cerr << "Failed to send request to set an effecter on Host \n";
        }
    }
}
void pldm::responder::oem_ibm_platform::Handler::monitorDump(
    const std::string& obj_path)
{
    std::string matchInterface = "xyz.openbmc_project.Common.Progress";
    sbeDumpMatch = std::make_unique<sdbusplus::bus::match::match>(
        pldm::utils::DBusHandler::getBus(),
        sdbusplus::bus::match::rules::propertiesChanged(obj_path.c_str(),
                                                        matchInterface.c_str()),
        [&](sdbusplus::message::message& msg) {
        DbusChangedProps props{};
        std::string intf;
        msg.read(intf, props);
        const auto itr = props.find("Status");
        if (itr != props.end())
        {
            PropertyValue value = itr->second;
            auto propVal = std::get<std::string>(value);
            if (propVal ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
            {
                setHostEffecterState(true);
            }
            else if (
                propVal ==
                    "xyz.openbmc_project.Common.Progress.OperationStatus.Failed" ||
                propVal ==
                    "xyz.openbmc_project.Common.Progress.OperationStatus.Aborted")
            {
                setHostEffecterState(false);
            }
        }
        sbeDumpMatch = nullptr;
        });
}

int pldm::responder::oem_ibm_platform::Handler::setNumericEffecter(
    uint16_t entityInstance, const PropertyValue& propertyValue)
{
    static constexpr auto objectPath = "/org/openpower/dump";
    static constexpr auto interface = "xyz.openbmc_project.Dump.Create";

    uint32_t value = std::get<uint32_t>(propertyValue);
    auto& bus = pldm::utils::DBusHandler::getBus();

    try
    {
        auto service = pldm::utils::DBusHandler().getService(objectPath,
                                                             interface);
        auto method = bus.new_method_call(service.c_str(), objectPath,
                                          interface, "CreateDump");

        std::map<std::string, std::variant<std::string, uint64_t>> createParams;
        createParams["com.ibm.Dump.Create.CreateParameters.DumpType"] =
            "com.ibm.Dump.Create.DumpType.SBE";
        createParams["com.ibm.Dump.Create.CreateParameters.ErrorLogId"] =
            (uint64_t)value;
        createParams["com.ibm.Dump.Create.CreateParameters.FailingUnitId"] =
            (uint64_t)entityInstance;
        method.append(createParams);

        auto response = bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());

        sdbusplus::message::object_path reply;
        response.read(reply);

        monitorDump(reply);
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Failed to make a DBus call as the dump policy is disabled,ERROR= "
            << e.what() << "\n";
        // case when the dump policy is disabled but we set the host effecter as
        // true and the host moves on
        setHostEffecterState(true);
    }
    return PLDM_SUCCESS;
}

std::vector<std::string> getProcObjectPaths()
{
    static constexpr auto searchpath = "/xyz/openbmc_project/inventory/system";
    int depth = 0;
    std::vector<std::string> procInterface = {
        "xyz.openbmc_project.Inventory.Item.Cpu"};
    pldm::utils::GetSubTreeResponse response =
        pldm::utils::DBusHandler().getSubtree(searchpath, depth, procInterface);
    std::vector<std::string> procPaths;
    for (const auto& [objPath, serviceMap] : response)
    {
        procPaths.emplace_back(objPath);
    }
    return procPaths;
}

void pldm::responder::oem_ibm_platform::Handler::sendStateSensorEvent(
    uint16_t sensorId, enum sensor_event_class_states sensorEventClass,
    uint8_t sensorOffset, uint8_t eventState, uint8_t prevEventState)
{
    std::vector<uint8_t> sensorEventDataVec{};
    size_t sensorEventSize = PLDM_SENSOR_EVENT_DATA_MIN_LENGTH + 1;
    sensorEventDataVec.resize(sensorEventSize);
    auto eventData = reinterpret_cast<struct pldm_sensor_event_data*>(
        sensorEventDataVec.data());
    eventData->sensor_id = sensorId;
    eventData->sensor_event_class_type = sensorEventClass;
    auto eventClassStart = eventData->event_class;
    auto eventClass =
        reinterpret_cast<struct pldm_sensor_event_state_sensor_state*>(
            eventClassStart);
    eventClass->sensor_offset = sensorOffset;
    eventClass->event_state = eventState;
    eventClass->previous_event_state = prevEventState;
    auto instanceId = instanceIdDb.next(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_PLATFORM_EVENT_MESSAGE_MIN_REQ_BYTES +
                                    sensorEventDataVec.size());
    auto rc = encodeEventMsg(PLDM_SENSOR_EVENT, sensorEventDataVec, requestMsg,
                             instanceId);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Failed to encode state sensor event, rc = " << rc
                  << std::endl;
        instanceIdDb.free(mctp_eid, instanceId);
        return;
    }
    rc = sendEventToHost(requestMsg, instanceId);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "Failed to send event to host: "
                  << "rc=" << rc << std::endl;
    }
    return;
}

void pldm::responder::oem_ibm_platform::Handler::_processEndUpdate(
    sdeventplus::source::EventBase& /*source */)
{
    assembleImageEvent.reset();
    std::cout << "Starting assembleCodeUpdateImage \n";
    int retc = codeUpdate->assembleCodeUpdateImage();
    if (retc != PLDM_SUCCESS)
    {
        codeUpdate->setCodeUpdateProgress(false);
        auto sensorId = codeUpdate->getFirmwareUpdateSensor();
        sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                             uint8_t(CodeUpdateState::FAIL),
                             uint8_t(CodeUpdateState::START));
    }
}

void pldm::responder::oem_ibm_platform::Handler::_processStartUpdate(
    sdeventplus::source::EventBase& /*source */)
{
    codeUpdate->deleteImage();
    CodeUpdateState state = CodeUpdateState::START;
    auto rc = codeUpdate->setRequestedApplyTime();
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "setRequestedApplyTime failed \n";
        state = CodeUpdateState::FAIL;
    }
    auto sensorId = codeUpdate->getFirmwareUpdateSensor();
    std::cout << "Sending Start Update sensor event to PHYP\n";
    sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0, uint8_t(state),
                         uint8_t(CodeUpdateState::END));
}

void pldm::responder::oem_ibm_platform::Handler::upadteOemDbusPaths(
    std::string& dbusPath)
{
    std::string toFind("system1/chassis1/motherboard1");
    if (dbusPath.find(toFind) != std::string::npos)
    {
        size_t pos = dbusPath.find(toFind);
        dbusPath.replace(pos, toFind.length(), "system/chassis/motherboard");
    }
    toFind = "system1";
    if (dbusPath.find(toFind) != std::string::npos)
    {
        size_t pos = dbusPath.find(toFind);
        dbusPath.replace(pos, toFind.length(), "system");
    }
    toFind = "socket";
    if (dbusPath.find(toFind) != std::string::npos)
    {
        std::regex reg(R"(\/motherboard\/socket[0-9]+)");
        dbusPath = regex_replace(dbusPath, reg, "/motherboard");
    }
}
void pldm::responder::oem_ibm_platform::Handler::_processSystemReboot(
    sdeventplus::source::EventBase& /*source */)
{
    BiosAttributeList biosAttrList;
    biosAttrList.push_back(std::make_pair("pvm_boot_initiator", "Host"));
    setBiosAttr(biosAttrList);
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.State.Chassis.Transition.Off";
    pldm::utils::DBusMapping dbusMapping{"/xyz/openbmc_project/state/chassis0",
                                         "xyz.openbmc_project.State.Chassis",
                                         "RequestedPowerTransition", "string"};
    try
    {
        std::cout << "InbandCodeUpdate: ChassifOff the host\n";
        dBusIntf->setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Chassis State transition to Off failed,"
                  << "unable to set property RequestedPowerTransition"
                  << "ERROR=" << e.what() << "\n";
    }

    using namespace sdbusplus::bus::match::rules;
    chassisOffMatch = std::make_unique<sdbusplus::bus::match::match>(
        pldm::utils::DBusHandler::getBus(),
        propertiesChanged("/xyz/openbmc_project/state/chassis0",
                          "xyz.openbmc_project.State.Chassis"),
        [this](sdbusplus::message::message& msg) {
        DbusChangedProps props{};
        std::string intf;
        msg.read(intf, props);
        const auto itr = props.find("CurrentPowerState");
        if (itr != props.end())
        {
            PropertyValue value = itr->second;
            auto propVal = std::get<std::string>(value);
            if (propVal == "xyz.openbmc_project.State.Chassis.PowerState.Off")
            {
                pldm::utils::DBusMapping dbusMapping{
                    "/xyz/openbmc_project/control/host0/"
                    "power_restore_policy/one_time",
                    "xyz.openbmc_project.Control.Power.RestorePolicy",
                    "PowerRestorePolicy", "string"};
                value = "xyz.openbmc_project.Control.Power.RestorePolicy."
                        "Policy.AlwaysOn";
                try
                {
                    std::cout
                        << "InbandCodeUpdate: Setting the one time APR policy\n";
                    dBusIntf->setDbusProperty(dbusMapping, value);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Setting one-time restore policy failed,"
                              << "unable to set property PowerRestorePolicy"
                              << "ERROR=" << e.what() << "\n";
                }
                dbusMapping = pldm::utils::DBusMapping{
                    "/xyz/openbmc_project/state/bmc0",
                    "xyz.openbmc_project.State.BMC", "RequestedBMCTransition",
                    "string"};
                value = "xyz.openbmc_project.State.BMC.Transition.Reboot";
                try
                {
                    std::cout << "InbandCodeUpdate: Rebooting the BMC\n";
                    dBusIntf->setDbusProperty(dbusMapping, value);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "BMC state transition to reboot failed,"
                              << "unable to set property "
                                 "RequestedBMCTransition"
                              << "ERROR=" << e.what() << "\n";
                }
            }
        }
        });
}

void pldm::responder::oem_ibm_platform::Handler::checkAndDisableWatchDog()
{
    if (!hostOff && setEventReceiverCnt == SET_EVENT_RECEIVER_SENT)
    {
        disableWatchDogTimer();
    }

    return;
}

bool pldm::responder::oem_ibm_platform::Handler::watchDogRunning()
{
    static constexpr auto watchDogObjectPath =
        "/xyz/openbmc_project/watchdog/host0";
    static constexpr auto watchDogEnablePropName = "Enabled";
    static constexpr auto watchDogInterface =
        "xyz.openbmc_project.State.Watchdog";
    bool isWatchDogRunning = false;
    try
    {
        isWatchDogRunning = pldm::utils::DBusHandler().getDbusProperty<bool>(
            watchDogObjectPath, watchDogEnablePropName, watchDogInterface);
    }
    catch (const std::exception& e)
    {
        return false;
    }
    return isWatchDogRunning;
}

void pldm::responder::oem_ibm_platform::Handler::resetWatchDogTimer()
{
    static constexpr auto watchDogService = "xyz.openbmc_project.Watchdog";
    static constexpr auto watchDogObjectPath =
        "/xyz/openbmc_project/watchdog/host0";
    static constexpr auto watchDogInterface =
        "xyz.openbmc_project.State.Watchdog";
    static constexpr auto watchDogResetPropName = "ResetTimeRemaining";

    bool wdStatus = watchDogRunning();
    if (wdStatus == false)
    {
        return;
    }
    try
    {
        auto& bus = pldm::utils::DBusHandler::getBus();
        auto resetMethod =
            bus.new_method_call(watchDogService, watchDogObjectPath,
                                watchDogInterface, watchDogResetPropName);
        resetMethod.append(true);
        bus.call_noreply(resetMethod);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed To reset watchdog timer"
                  << "ERROR=" << e.what() << std::endl;
        return;
    }
}

void pldm::responder::oem_ibm_platform::Handler::disableWatchDogTimer()
{
    setEventReceiverCnt = 0;
    pldm::utils::DBusMapping dbusMapping{"/xyz/openbmc_project/watchdog/host0",
                                         "xyz.openbmc_project.State.Watchdog",
                                         "Enabled", "bool"};
    bool wdStatus = watchDogRunning();

    if (!wdStatus)
    {
        return;
    }
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, false);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed To disable watchdog timer"
                  << "ERROR=" << e.what() << "\n";
    }
}
int pldm::responder::oem_ibm_platform::Handler::checkBMCState()
{
    try
    {
        pldm::utils::PropertyValue propertyValue =
            pldm::utils::DBusHandler().getDbusPropertyVariant(
                "/xyz/openbmc_project/state/bmc0", "CurrentBMCState",
                "xyz.openbmc_project.State.BMC");

        if (std::get<std::string>(propertyValue) !=
            "xyz.openbmc_project.State.BMC.BMCState.Ready")
        {
            std::cerr << "GetPDR : PLDM stack is not ready for PDR exchange"
                      << std::endl;
            return PLDM_ERROR_NOT_READY;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error getting the current BMC state" << std::endl;
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

void pldm::responder::oem_ibm_platform::Handler::setBitmapMethodCall(
    const char* service, const char* objPath, const char* dbusMethod,
    const char* dbusInterface, const PropertyValue& value)
{
    try
    {
        auto& bus = pldm::utils::DBusHandler::getBus();
        auto method = bus.new_method_call(service, objPath, dbusInterface,
                                          dbusMethod);
        auto val = std::get_if<std::vector<uint8_t>>(&value);
        method.append(*val);
        bus.call_noreply(method);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to call the D-Bus Method"
                  << "ERROR=" << e.what() << std::endl;
        return;
    }
}

void pldm::responder::oem_ibm_platform::Handler::modifyPDROemActions(
    uint16_t entityType, uint16_t stateSetId)
{
    pldm::pdr::EntityType pdrEntityType = entityType;
    pldm::pdr::StateSetId pdrStateSetId = stateSetId;
    if ((pdrEntityType == (PLDM_ENTITY_CHASSIS_FRONT_PANEL_BOARD | 0x8000)) &&
        (pdrStateSetId == PLDM_OEM_IBM_PANEL_TRIGGER_STATE))
    {
        auto pdrs = pldm::utils::findStateEffecterPDR(0, pdrEntityType,
                                                      pdrStateSetId, pdrRepo);
        if (!std::empty(pdrs))
        {
            auto bitMap = responder::pdr_utils::fetchBitMap(pdrs);
            setBitmapMethodCall("com.ibm.PanelApp", "/com/ibm/panel_app",
                                "toggleFunctionState", "com.ibm.panel", bitMap);
        }
    }
}

void pldm::responder::oem_ibm_platform::Handler::updateContainerID()
{
    for (auto& [key, value] : instanceMap)
    {
        uint16_t newContainerID = pldm_find_container_id(
            pdrRepo, PLDM_ENTITY_PROC_MODULE, value.dcmId);
        pldm_change_container_id_of_effecter(pdrRepo, key, newContainerID);
    }
}

void pldm::responder::oem_ibm_platform::Handler::handleBootTypesAtPowerOn()
{
    BiosAttributeList biosAttrList;
    auto bootInitiator = getBiosAttrValue("pvm_boot_initiator_current");
    std::string restartCause;
    if (((bootInitiator != "HMC") || (bootInitiator != "Host")) &&
        !bootInitiator.empty())
    {
        try
        {
            restartCause =
                pldm::utils::DBusHandler().getDbusProperty<std::string>(
                    "/xyz/openbmc_project/state/host0", "RestartCause",
                    "xyz.openbmc_project.State.Host");
            setBootTypesBiosAttr(restartCause);
        }
        catch (const std::exception& e)
        {}
    }
}

void pldm::responder::oem_ibm_platform::Handler::setBootTypesBiosAttr(
    const std::string& restartCause)
{
    BiosAttributeList biosAttrList;
    if (restartCause ==
        "xyz.openbmc_project.State.Host.RestartCause.ScheduledPowerOn")
    {
        biosAttrList.push_back(std::make_pair("pvm_boot_initiator", "Host"));
        setBiosAttr(biosAttrList);
        stateManagerMatch.reset();
    }
    else if (
        (restartCause ==
         "xyz.openbmc_project.State.Host.RestartCause.PowerPolicyAlwaysOn") ||
        (restartCause ==
         "xyz.openbmc_project.State.Host.RestartCause.PowerPolicyPreviousState"))
    {
        biosAttrList.push_back(std::make_pair("pvm_boot_initiator", "Auto"));
        setBiosAttr(biosAttrList);
        stateManagerMatch.reset();
    }
    else if (restartCause ==
             "xyz.openbmc_project.State.Host.RestartCause.HostCrash")
    {
        biosAttrList.push_back(std::make_pair("pvm_boot_initiator", "Auto"));
        biosAttrList.push_back(std::make_pair("pvm_boot_type", "ReIPL"));
        setBiosAttr(biosAttrList);
        stateManagerMatch.reset();
    }
}

void pldm::responder::oem_ibm_platform::Handler::handleBootTypesAtChassisOff()
{
    BiosAttributeList biosAttrList;
    auto bootInitiator = getBiosAttrValue("pvm_boot_initiator");
    auto bootType = getBiosAttrValue("pvm_boot_type");
    if (bootInitiator.empty() || bootType.empty())
    {
        std::cerr
            << "ERROR in fetching the pvm_boot_initiator and pvm_boot_type BIOS attribute values\n";
        return;
    }
    else if (bootInitiator != "Host")
    {
        biosAttrList.push_back(std::make_pair("pvm_boot_initiator", "User"));
        biosAttrList.push_back(std::make_pair("pvm_boot_type", "IPL"));
        setBiosAttr(biosAttrList);
    }
}

void pldm::responder::oem_ibm_platform::Handler::turnOffRealSAIEffecter()
{
    try
    {
        pldm::utils::DBusMapping dbuspartitionMapping{
            "/xyz/openbmc_project/led/groups/partition_system_attention_indicator",
            "xyz.openbmc_project.Led.Group", "Asserted", "bool"};
        pldm::utils::DBusHandler().setDbusProperty(dbuspartitionMapping, false);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to turn off partition SAI effecter"
                  << "ERROR=" << e.what() << "\n";
    }
    try
    {
        pldm::utils::DBusMapping dbusplatformMapping{
            "/xyz/openbmc_project/led/groups/platform_system_attention_indicator",
            "xyz.openbmc_project.Led.Group", "Asserted", "bool"};
        pldm::utils::DBusHandler().setDbusProperty(dbusplatformMapping, false);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to turn off platform SAI effecter"
                  << "ERROR=" << e.what() << "\n";
    }
}

uint8_t pldm::responder::oem_ibm_platform::Handler::fetchRealSAIStatus()
{
    static constexpr auto partitionSAIObjectPath =
        "/xyz/openbmc_project/led/groups/partition_system_attention_indicator";
    static constexpr auto platformSAIObjectPath =
        "/xyz/openbmc_project/led/groups/platform_system_attention_indicator";
    static constexpr auto saiInterface = "xyz.openbmc_project.Led.Group";
    static constexpr auto saiPropertyName = "Asserted";
    uint8_t isPartitionSAIOn = 0;
    uint8_t isPlatformSAIOn = 0;

    try
    {
        isPartitionSAIOn = pldm::utils::DBusHandler().getDbusProperty<bool>(
            partitionSAIObjectPath, saiPropertyName, saiInterface);
        isPlatformSAIOn = pldm::utils::DBusHandler().getDbusProperty<bool>(
            platformSAIObjectPath, saiPropertyName, saiInterface);

        if (isPartitionSAIOn || isPlatformSAIOn)
        {
            return PLDM_SENSOR_WARNING;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to fetch Real SAI sensor status"
                  << "ERROR=" << e.what() << "\n";
    }
    return PLDM_SENSOR_NORMAL;
}

void pldm::responder::oem_ibm_platform::Handler::
    processPowerCycleOffSoftGraceful()
{
    std::cerr << "Received soft graceful power cycle request" << std::endl;
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.State.Host.Transition.ForceWarmReboot";
    pldm::utils::DBusMapping dbusMapping{"/xyz/openbmc_project/state/host0",
                                         "xyz.openbmc_project.State.Host",
                                         "RequestedHostTransition", "string"};
    try
    {
        dBusIntf->setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Error to do a ForceWarmReboot, chassis power remains on, and boot the host back up. Unable to set property RequestedHostTransition. ERROR="
            << e.what() << "\n";
    }
}

void pldm::responder::oem_ibm_platform::Handler::processPowerOffSoftGraceful()
{
    std::cerr << "Received soft power off graceful request" << std::endl;
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.State.Chassis.Transition.Off";
    pldm::utils::DBusMapping dbusMapping{"/xyz/openbmc_project/state/chassis0",
                                         "xyz.openbmc_project.State.Chassis",
                                         "RequestedPowerTransition", "string"};
    try
    {
        dBusIntf->setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Error in powering down the host. Unable to set property RequestedPowerTransition. ERROR="
            << e.what() << "\n";
    }
}

void pldm::responder::oem_ibm_platform::Handler::processPowerOffHardGraceful()
{
    std::cerr << "Received hard power off graceful request" << std::endl;
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.Control.Power.RestorePolicy.Policy.AlwaysOn";
    pldm::utils::DBusMapping dbusMapping{
        "/xyz/openbmc_project/control/host0/power_restore_policy/one_time",
        "xyz.openbmc_project.Control.Power.RestorePolicy", "PowerRestorePolicy",
        "string"};
    try
    {
        dBusIntf->setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Setting one-time restore policy failed, Unable to set property PowerRestorePolicy. ERROR="
            << e.what() << "\n";
    }
    processPowerOffSoftGraceful();
}

void pldm::responder::oem_ibm_platform::Handler::startStopTimer(bool value)
{
    if (value)
    {
        timer.restart(
            std::chrono::seconds(HEARTBEAT_TIMEOUT + HEARTBEAT_TIMEOUT_DELTA));
    }
    else
    {
        timer.setEnabled(value);
    }
}

void pldm::responder::oem_ibm_platform::Handler::setSurvTimer(uint8_t tid,
                                                              bool value)
{
    if ((hostOff == true) || (hostTransitioningToOff == true) ||
        (tid != HYPERVISOR_TID))
    {
        if (timer.isEnabled())
        {
            startStopTimer(false);
        }
        return;
    }
    if (value)
    {
        startStopTimer(true);
    }
    else if (!value && timer.isEnabled())
    {
        std::cout << "setSurvTimer:LogginPel:hostOff=" << (bool)hostOff
                  << " hostTransitioningToOff=" << (bool)hostTransitioningToOff
                  << " tid=" << (uint16_t)tid << std::endl;
        startStopTimer(false);
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.setSurvTimer.RecvSurveillancePingFail",
            pldm::PelSeverity::INFORMATIONAL);
    }
}

void pldm::responder::oem_ibm_platform::Handler::propertyChanged(
    const DbusChangedProps& chProperties, std::string objPath)
{
    std::cerr << "Got a link reset set for CEC path: " << objPath << std::endl;
    static constexpr auto propName = "linkReset";
    const auto it = chProperties.find(propName);
    if (it == chProperties.end())
    {
        return;
    }

    bool Value = std::get<bool>(it->second);
    triggerHostEffecter(Value, objPath);
}

void pldm::responder::oem_ibm_platform::Handler::createMatches()
{
    /* Already setup */
    if (!this->matches.empty())
    {
        return;
    }

    /* Creating matches*/
    using namespace sdbusplus::bus::match::rules;
    std::vector<std::string> slotObjects;
    static constexpr auto boardObjPath =
        "/xyz/openbmc_project/inventory/system/chassis/motherboard";

    pldm::responder::utils::findSlotObjects(boardObjPath, slotObjects);

    for (const std::string& slot : slotObjects)
    {
        matches.push_back(std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            propertiesChanged(slot, "com.ibm.Control.Host.PCIeLink"),
            [this, slot](sdbusplus::message::message& msg) {
            DbusChangedProps props;
            std::string iface;
            msg.read(iface, props);
            propertyChanged(props, slot);
            }));
    }
}

void pldm::responder::oem_ibm_platform::Handler::triggerHostEffecter(
    bool value, std::string path)
{
    std::vector<set_effecter_state_field> stateField;

    stateField.push_back({PLDM_REQUEST_SET, PLDM_RESETTING});

    uint16_t effecterID = 0;
    if (value && hostEffecterParser)
    {
        pldm::pdr::EntityType entityType = PLDM_ENTITY_PCI_EXPRESS_BUS | 0x8000;
        auto stateEffecterPDRs = pldm::utils::findStateEffecterPDR(
            mctp_eid, entityType,
            static_cast<uint16_t>(PLDM_STATE_SET_AVAILABILITY), pdrRepo);

        if (stateEffecterPDRs.empty())
        {
            std::cerr
                << "PCIe CEC LinkReset: The state set PDR can not be found, entityType = "
                << entityType << std::endl;
            return;
        }

        static constexpr auto propName = "BusId";
        static constexpr auto interface =
            "xyz.openbmc_project.Inventory.Item.PCIeSlot";
        uint32_t instanceId = 0;

        try
        {
            instanceId = pldm::utils::DBusHandler().getDbusProperty<uint32_t>(
                path.c_str(), propName, interface);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to fetch the BusID of the slot. ERROR= "
                      << e.what() << std::endl;
            return;
        }

        for (auto& rep : stateEffecterPDRs)
        {
            auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(rep.data());
            if (instanceId == (uint32_t)pdr->entity_instance)
            {
                effecterID = pdr->effecter_id;
                break;
            }
        }

        std::cerr << "Sending effecter call to host with effecter ID : "
                  << effecterID << "and BusID : " << instanceId << std::endl;
        hostEffecterParser->sendSetStateEffecterStates(
            mctp_eid, effecterID, 1, stateField, nullptr, value);

        std::cerr << "Setting link reset on link: " << path
                  << " is successful setting it back to false" << std::endl;
        pldm::utils::DBusMapping dbusMapping;
        dbusMapping.objectPath = path;
        dbusMapping.interface = "com.ibm.Control.Host.PCIeLink";
        dbusMapping.propertyName = "linkReset";
        dbusMapping.propertyType = "bool";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, false);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to set the link reset property. ERROR = "
                      << e.what() << std::endl;
            return;
        }
    }
}
} // namespace oem_ibm_platform
} // namespace responder
} // namespace pldm
