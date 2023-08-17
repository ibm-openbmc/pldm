#include "fru.hpp"

#include "common/utils.hpp"
#include "pdr.hpp"

#ifdef OEM_IBM
#include "oem/ibm/libpldmresponder/utils.hpp"

#include <libpldm/pdr_oem_ibm.h>
#endif

#include <libpldm/entity.h>
#include <libpldm/utils.h>
#include <systemd/sd-journal.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>

#include <iostream>
#include <set>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{
pldm_entity FruImpl::getEntityByObjectPath(const dbus::ObjectValueTree& objects,
                                           const std::string& path)
{
    pldm_entity entity{};

    auto iter = objects.find(path);
    if (iter == objects.end())
    {
        return entity;
    }

    for (const auto& intfMap : iter->second)
    {
        try
        {
            entity.entity_type = parser.getEntityType(intfMap.first);
            entity.entity_instance_num = 1;
            break;
        }
        catch (const std::exception& e)
        {
            continue;
        }
    }

    return entity;
}

void FruImpl::updateAssociationTree(const dbus::ObjectValueTree& objects,
                                    const std::string& path)
{
    // eg: path =
    // "/xyz/openbmc_project/inventory/system/chassis/motherboard/powersupply0"
    // Get the parent class in turn and store it in a temporary vector
    // eg: tmpObjPaths = {"/xyz/openbmc_project/inventory/system",
    // "/xyz/openbmc_project/inventory/system/chassis/",
    // "/xyz/openbmc_project/inventory/system/chassis/motherboard",
    // "/xyz/openbmc_project/inventory/system/chassis/motherboard/powersupply0"}
    std::string root = "/xyz/openbmc_project/inventory";
    if (path.find(root + "/") == std::string::npos)
    {
        return;
    }

    std::vector<std::string> tmpObjPaths{};
    tmpObjPaths.push_back(path);

    auto obj = pldm::utils::findParent(path);
    while (obj != root)
    {
        tmpObjPaths.push_back(obj);
        obj = pldm::utils::findParent(obj);
    }

    // Update pldm entity to assocition tree
    int i = (int)tmpObjPaths.size() - 1;
    for (; i >= 0; i--)
    {
        if (objToEntityNode.contains(tmpObjPaths[i]))
        {
            pldm_entity_node* node = nullptr;
            pldm_find_entity_ref_in_tree(
                entityTree, objToEntityNode.at(tmpObjPaths[i]), &node);
            pldm_entity node_entity = pldm_entity_extract(node);
            if (pldm_entity_association_tree_find(entityTree, &node_entity,
                                                  false))
            {
                continue;
            }
        }
        else
        {
            pldm_entity entity = getEntityByObjectPath(objects, tmpObjPaths[i]);
            if (entity.entity_type == 0 && entity.entity_instance_num == 0 &&
                entity.entity_container_id == 0)
            {
                continue;
            }

            for (auto& it : objToEntityNode)
            {
                pldm_entity node = it.second;
                if (node.entity_type == entity.entity_type)
                {
                    entity.entity_instance_num = node.entity_instance_num + 1;
                    break;
                }
            }

            if (i == (int)tmpObjPaths.size() - 1)
            {
                auto node = pldm_entity_association_tree_add(
                    entityTree, &entity, 0xFFFF, nullptr,
                    PLDM_ENTITY_ASSOCIAION_PHYSICAL, false, true, 0xFFFF);
                objToEntityNode[tmpObjPaths[i]] = pldm_entity_extract(node);
            }
            else
            {
                if (objToEntityNode.contains(tmpObjPaths[i + 1]))
                {
                    pldm_entity_node* parent_node = nullptr;
                    pldm_find_entity_ref_in_tree(
                        entityTree, objToEntityNode[tmpObjPaths[i + 1]],
                        &parent_node);
                    auto node = pldm_entity_association_tree_add(
                        entityTree, &entity, 0xFFFF, parent_node,
                        PLDM_ENTITY_ASSOCIAION_PHYSICAL, false, true, 0xFFFF);
                    objToEntityNode[tmpObjPaths[i]] = pldm_entity_extract(node);
                }
            }
        }
    }
}

void FruImpl::buildFRUTable()
{
    if (isBuilt)
    {
        return;
    }

    subscribeFruPresence(inventoryObjPath, fanInterface, itemInterface,
                         fanHotplugMatch);
    subscribeFruPresence(inventoryObjPath, psuInterface, itemInterface,
                         psuHotplugMatch);
    subscribeFruPresence(inventoryObjPath, pcieAdapterInterface, itemInterface,
                         pcieHotplugMatch);
    subscribeFruPresence(inventoryObjPath, panelInterface, itemInterface,
                         panelHotplugMatch);

    fru_parser::DBusLookupInfo dbusInfo;

    // Read the all the inventory D-Bus objects
    try
    {
        dbusInfo = parser.inventoryLookup();
        objects = pldm::utils::DBusHandler::getInventoryObjects();
    }
    catch (const std::exception& e)
    {
        info(
            "Look up of inventory objects failed and PLDM FRU table creation failed");
        return;
    }

    auto itemIntfsLookup = std::get<2>(dbusInfo);

    for (const auto& object : objects)
    {
        const auto& interfaces = object.second;
        bool isPresent = true;
#ifdef OEM_IBM
        isPresent =
            pldm::responder::utils::checkFruPresence(object.first.str.c_str());
#endif
        if (!isPresent)
        {
            continue;
        }
        for (const auto& interface : interfaces)
        {
            if (itemIntfsLookup.find(interface.first) != itemIntfsLookup.end())
            {
                // An exception will be thrown by getRecordInfo, if the item
                // D-Bus interface name specified in FRU_Master.json does
                // not have corresponding config jsons
                try
                {
                    updateAssociationTree(objects, object.first.str);
                    pldm_entity entity{};
                    if (objToEntityNode.contains(object.first.str))
                    {
                        pldm_entity_node* node = nullptr;
                        pldm_find_entity_ref_in_tree(
                            entityTree, objToEntityNode.at(object.first.str),
                            &node);
                        entity = pldm_entity_extract(node);
                    }

                    auto recordInfos = parser.getRecordInfo(interface.first);
                    populateRecords(interfaces, recordInfos, entity,
                                    object.first);
                    associatedEntityMap.emplace(object.first, entity);
                    break;
                }
                catch (const std::exception& e)
                {
                    info(
                        "Config JSONs missing for the item interface type, interface = {INTF}",
                        "INTF", interface.first);
                    break;
                }
            }
        }
    }

    pldm_entity_association_pdr_add(entityTree, pdrRepo, false,
                                    TERMINUS_HANDLE);
    // save a copy of bmc's entity association tree
    pldm_entity_association_tree_copy_root(entityTree, bmcEntityTree);

    isBuilt = true;
}
std::string FruImpl::populatefwVersion()
{
    static constexpr auto fwFunctionalObjPath =
        "/xyz/openbmc_project/software/functional";
    auto& bus = pldm::utils::DBusHandler::getBus();
    std::string currentBmcVersion;
    try
    {
        auto method = bus.new_method_call(pldm::utils::mapperService,
                                          fwFunctionalObjPath,
                                          pldm::utils::dbusProperties, "Get");
        method.append("xyz.openbmc_project.Association", "endpoints");
        std::variant<std::vector<std::string>> paths;
        auto reply = bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
        reply.read(paths);
        auto fwRunningVersion = std::get<std::vector<std::string>>(paths)[0];
        constexpr auto versionIntf = "xyz.openbmc_project.Software.Version";
        auto version = pldm::utils::DBusHandler().getDbusPropertyVariant(
            fwRunningVersion.c_str(), "Version", versionIntf);
        currentBmcVersion = std::get<std::string>(version);
    }
    catch (const std::exception& e)
    {
        error("failed to make a d-bus call Asociation, ERROR= {ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        return {};
    }
    return currentBmcVersion;
}
uint32_t FruImpl::populateRecords(
    const pldm::responder::dbus::InterfaceMap& interfaces,
    const fru_parser::FruRecordInfos& recordInfos, const pldm_entity& entity,
    const dbus::ObjectPath& objectPath, bool concurrentAdd)
{
    // recordSetIdentifier for the FRU will be set when the first record gets
    // added for the FRU
    uint16_t recordSetIdentifier = 0;
    auto numRecsCount = numRecs;
    static uint32_t bmc_record_handle = 0;
    uint32_t newRcord{};

    for (const auto& [recType, encType, fieldInfos] : recordInfos)
    {
        std::vector<uint8_t> tlvs;
        uint8_t numFRUFields = 0;
        for (const auto& [intf, prop, propType, fieldTypeNum] : fieldInfos)
        {
            try
            {
                pldm::responder::dbus::Value propValue;
                if (entity.entity_type == 11521 && prop == "Version")
                {
                    propValue = populatefwVersion();
                }
                else
                {
                    propValue = interfaces.at(intf).at(prop);
                }
                if (propType == "bytearray")
                {
                    auto byteArray = std::get<std::vector<uint8_t>>(propValue);
                    if (!byteArray.size())
                    {
                        continue;
                    }

                    numFRUFields++;
                    tlvs.emplace_back(fieldTypeNum);
                    tlvs.emplace_back(byteArray.size());
                    std::move(std::begin(byteArray), std::end(byteArray),
                              std::back_inserter(tlvs));
                }
                else if (propType == "string")
                {
                    auto str = std::get<std::string>(propValue);
                    if (!str.size())
                    {
                        continue;
                    }

                    numFRUFields++;
                    tlvs.emplace_back(fieldTypeNum);
                    tlvs.emplace_back(str.size());
                    std::move(std::begin(str), std::end(str),
                              std::back_inserter(tlvs));
                }
            }
            catch (const std::out_of_range& e)
            {
                continue;
            }
        }

        if (tlvs.size())
        {
            if (numRecs == numRecsCount)
            {
                recordSetIdentifier = nextRSI();
                if (concurrentAdd)
                {
#ifdef OEM_IBM
                    auto lastLocalRecord =
                        pldm_pdr_find_last_local_record(pdrRepo);
                    bmc_record_handle = (lastLocalRecord->record_handle) + 1;
#endif
                }
                else
                {
                    bmc_record_handle = nextRecordHandle();
                }
                int rc = pldm_pdr_add_fru_record_set_check(
                    pdrRepo, TERMINUS_HANDLE, recordSetIdentifier,
                    entity.entity_type, entity.entity_instance_num,
                    entity.entity_container_id, &bmc_record_handle,
                    concurrentAdd);
                if (rc)
                {
                    // pldm_pdr_add_fru_record_set() assert()ed on failure
                    throw std::runtime_error(
                        "Failed to add PDR FRU record set");
                }
                newRcord = bmc_record_handle;
                objectPathToRSIMap[objectPath] = recordSetIdentifier;
            }
            auto curSize = table.size();
            table.resize(curSize + recHeaderSize + tlvs.size());
            encode_fru_record(table.data(), table.size(), &curSize,
                              recordSetIdentifier, recType, numFRUFields,
                              encType, tlvs.data(), tlvs.size());
            numRecs++;
        }
    }
    return newRcord;
}

void FruImpl::removeIndividualFRU(const std::string& fruObjPath)
{
    uint16_t rsi = objectPathToRSIMap[fruObjPath];
    pldm_entity removeEntity;
    uint16_t terminusHdl{};
    uint16_t entityType{};
    uint16_t entityInsNum{};
    uint16_t containerId{};
    pldm_pdr_fru_record_set_find_by_rsi(pdrRepo, rsi, &terminusHdl, &entityType,
                                        &entityInsNum, &containerId, false);
    removeEntity.entity_type = entityType;
    removeEntity.entity_instance_num = entityInsNum;
    removeEntity.entity_container_id = containerId;

    uint8_t bmcEventDataOps = PLDM_INVALID_OP;
    uint8_t hostEventDataOps = PLDM_INVALID_OP;
    auto updateRecordHdlBmc =
        pldm_entity_association_pdr_remove_contained_entity(
            pdrRepo, removeEntity, &bmcEventDataOps, false);

    auto updateRecordHdlHost =
        pldm_entity_association_pdr_remove_contained_entity(
            pdrRepo, removeEntity, &hostEventDataOps, true);

    auto deleteRecordHdl = pldm_pdr_remove_fru_record_set_by_rsi(pdrRepo, rsi,
                                                                 false);

    // sm00
    /* std::cout << "\nprinting the entityTree before deleting node\n";
     size_t num{};
     pldm_entity* out = nullptr;
     pldm_entity_association_tree_visit(entityTree,&out, &num);
     free(out);*/
    // sm00
    pldm_entity_association_tree_delete_node(entityTree, removeEntity);
    // sm00
    /*std::cout << "\nprinting the entityTree after deleting node\n";
    num = 0;
    out = nullptr;
    pldm_entity_association_tree_visit(entityTree,&out, &num);
    free(out);*/
    // sm00
    pldm_entity_association_tree_delete_node(bmcEntityTree, removeEntity);

    objectPathToRSIMap.erase(fruObjPath);
    objToEntityNode.erase(fruObjPath); // sm00
    error(
        "Removing Individual FRU [ {FRU_OBJ_PATH} ] with entityid [ {ENTITY_TYP}, {ENTITY_NUM}, {ENTITY_ID} ]",
        "FRU_OBJ_PATH", fruObjPath, "ENTITY_TYP",
        static_cast<unsigned>(removeEntity.entity_type), "ENTITY_NUM",
        static_cast<unsigned>(removeEntity.entity_instance_num), "ENTITY_ID",
        static_cast<unsigned>(removeEntity.entity_container_id));
    associatedEntityMap.erase(fruObjPath); // sm00

    deleteFruRecord(rsi);

    std::vector<ChangeEntry> handlesTobeDeleted;
    if (deleteRecordHdl != 0)
    {
        handlesTobeDeleted.push_back(deleteRecordHdl);
    }

    std::vector<uint16_t> effecterIDs = findEffecterIds(
        pdrRepo, 0 /*tid*/, removeEntity.entity_type,
        removeEntity.entity_instance_num, removeEntity.entity_container_id);

    for (const auto& ids : effecterIDs)
    {
        auto delEffecterHdl = pldm_delete_by_effecter_id(pdrRepo, ids, false);
        effecterDbusObjMaps.erase(ids);
        if (delEffecterHdl != 0)
        {
            handlesTobeDeleted.push_back(delEffecterHdl);
        }
    }
    std::vector<uint16_t> sensorIDs = findSensorIds(
        pdrRepo, 0 /*tid*/, removeEntity.entity_type,
        removeEntity.entity_instance_num, removeEntity.entity_container_id);

    for (const auto& ids : sensorIDs)
    {
        auto delSensorHdl = pldm_delete_by_sensor_id(pdrRepo, ids, false);
        sensorDbusObjMaps.erase(ids);
        if (delSensorHdl != 0)
        {
            handlesTobeDeleted.push_back(delSensorHdl);
        }
    }

    // need to
    // send both remote and local records. Phyp keeps track of bmc only records
    std::vector<ChangeEntry> handlesTobeModified;
    if (bmcEventDataOps != PLDM_INVALID_OP && updateRecordHdlBmc != 0)
    {
        (bmcEventDataOps == PLDM_RECORDS_DELETED)
            ? handlesTobeDeleted.push_back(updateRecordHdlBmc)
            : handlesTobeModified.push_back(updateRecordHdlBmc);
    }
    if (hostEventDataOps != PLDM_INVALID_OP && updateRecordHdlHost != 0)
    {
        (hostEventDataOps == PLDM_RECORDS_DELETED)
            ? handlesTobeDeleted.push_back(updateRecordHdlHost)
            : handlesTobeModified.push_back(updateRecordHdlHost);
    } // sm00 this can be RECORDS_DELETED also for adapter pdrs
    if (!handlesTobeDeleted.empty())
    {
        sendPDRRepositoryChgEventbyPDRHandles(
            std::move(handlesTobeDeleted),
            std::move(std::vector<uint8_t>(1, PLDM_RECORDS_DELETED)));
    }
    if (!handlesTobeModified.empty())
    {
        sendPDRRepositoryChgEventbyPDRHandles(
            std::move(handlesTobeModified),
            std::move(std::vector<uint8_t>(1, PLDM_RECORDS_MODIFIED)));
    }
}

void FruImpl::deleteFruRecord(uint16_t rsi)
{
    std::vector<uint8_t> updatedFruTbl;
    const struct pldm_fru_record_data_format* recordSetSrc =
        reinterpret_cast<const struct pldm_fru_record_data_format*>(
            table.data());

    const struct pldm_fru_record_tlv* tlv;
    size_t pos = 0;

    while ((table.size() > pos) && (recordSetSrc != nullptr))
    {
        size_t recordLen = sizeof(struct pldm_fru_record_data_format) -
                           sizeof(struct pldm_fru_record_tlv);

        tlv = recordSetSrc->tlvs;

        for (uint8_t i = 0; i < recordSetSrc->num_fru_fields; i++)
        {
            size_t len = sizeof(*tlv) - 1 + tlv->length;
            recordLen += len;
            tlv = reinterpret_cast<const struct pldm_fru_record_tlv*>(
                (char*)tlv + len);
        }
        if ((recordSetSrc->record_set_id != htole16(rsi) && rsi != 0))
        {
            std::copy(table.begin() + pos, table.begin() + pos + recordLen,
                      std::back_inserter(updatedFruTbl));
        }
        else
        {
            numRecs--;
        }

        pos += recordLen;
        recordSetSrc =
            reinterpret_cast<const struct pldm_fru_record_data_format*>(tlv);
    }

    table.clear();
    table = std::move(updatedFruTbl);
}

void FruImpl::buildIndividualFRU(const std::string& fruInterface,
                                 const std::string& fruObjectPath)
{
    // An exception will be thrown by getRecordInfo, if the item
    // D-Bus interface name specified in FRU_Master.json does
    // not have corresponding config jsons
    pldm_entity parent = {};
    pldm_entity entity{};
    pldm_entity parentEntity{};
    static uint32_t last_bmc_record_handle = 0;
    uint32_t newRecordHdl{};
    try
    {
        entity.entity_type = parser.getEntityType(fruInterface);
        auto parentObj = pldm::utils::findParent(fruObjectPath);
        do
        {
            auto iter = objToEntityNode.find(parentObj);
            if (iter != objToEntityNode.end())
            {
                parent = iter->second;
                break;
            }
            parentObj = pldm::utils::findParent(parentObj);
        } while (parentObj != "/");

        // sm00
        /* std::cout << "\nprinting the entityTree before adding the node" <<
         std::endl; size_t num = 0; pldm_entity* out = nullptr;
         pldm_entity_association_tree_visit(entityTree,&out, &num);
         free(out);*/
        // sm00

        pldm_entity_node* parent_node = nullptr;
        pldm_find_entity_ref_in_tree(entityTree, parent, &parent_node);
        uint16_t last_container_id = next_container_id(entityTree);
        auto node = pldm_entity_association_tree_add(
            entityTree, &entity, 0xFFFF, parent_node,
            PLDM_ENTITY_ASSOCIAION_PHYSICAL, false, true, last_container_id);
        pldm_entity node_entity = pldm_entity_extract(node);
        objToEntityNode[fruObjectPath] = node_entity;
        error(
            "Building Individual FRU [FRU_OBJ_PATH] with entityid [ {ENTITY_TYP}, {ENTITY_NUM}, {ENTITY_ID} ] Parent :[ {P_ENTITY_TYP}, {P_ENTITY_NUM}, {P_ENTITY_ID} ]",
            "FRU_OBJ_PATH", fruObjectPath, "ENTITY_TYP",
            static_cast<unsigned>(node_entity.entity_type), "ENTITY_NUM",
            static_cast<unsigned>(node_entity.entity_instance_num), "ENTITY_ID",
            static_cast<unsigned>(node_entity.entity_container_id),
            "P_ENTITY_TYP", static_cast<unsigned>(parent.entity_type),
            "P_ENTITY_NUM", static_cast<unsigned>(parent.entity_instance_num),
            "P_ENTITY_ID", static_cast<unsigned>(parent.entity_container_id));
        auto recordInfos = parser.getRecordInfo(fruInterface);

        // sm00
        /* std::cout << "\nprinting the entityTree after adding the node" <<
         std::endl; num = 0; out = nullptr;
         pldm_entity_association_tree_visit(entityTree,&out, &num);
         free(out);*/
        // sm00

        memcpy(reinterpret_cast<void*>(&parentEntity),
               reinterpret_cast<void*>(parent_node), sizeof(pldm_entity));
        pldm_entity_node* bmcTreeParentNode = nullptr;
        pldm_find_entity_ref_in_tree(bmcEntityTree, parentEntity,
                                     &bmcTreeParentNode);

        pldm_entity_association_tree_add(
            bmcEntityTree, &entity, 0xFFFF, bmcTreeParentNode,
            PLDM_ENTITY_ASSOCIAION_PHYSICAL, false, true, last_container_id);

        for (const auto& object : objects)
        {
            if (object.first.str == fruObjectPath)
            {
                const auto& interfaces = object.second;
                newRecordHdl = populateRecords(interfaces, recordInfos, entity,
                                               fruObjectPath, true);
                associatedEntityMap.emplace(fruObjectPath, entity);
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        info(
            "Config JSONs missing for the item in concurrent add path interface type, interface = {FRU_INTF}",
            "FRU_INTF", fruInterface);
    }
#ifdef OEM_IBM
    auto lastLocalRecord = pldm_pdr_find_last_local_record(pdrRepo);
    last_bmc_record_handle = lastLocalRecord->record_handle;
#endif

    uint8_t bmcEventDataOps = PLDM_INVALID_OP;
    auto updatedRecordHdlBmc = pldm_entity_association_pdr_add_contained_entity(
        pdrRepo, entity, parentEntity, &bmcEventDataOps, false,
        last_bmc_record_handle);
#ifdef OEM_IBM
    auto lastBMCRecord = pldm_pdr_find_last_local_record(pdrRepo);
    last_bmc_record_handle = lastBMCRecord->record_handle;
#endif

    uint8_t hostEventDataOps = PLDM_INVALID_OP;

    auto updatedRecordHdlHost =
        pldm_entity_association_pdr_add_contained_entity(
            pdrRepo, entity, parentEntity, &hostEventDataOps, true,
            last_bmc_record_handle);

    // create the relevant state effecter and sensor PDRs for the new fru record
    std::vector<uint32_t> recordHdlList;
    reGenerateStatePDR(fruObjectPath, recordHdlList);

    std::vector<ChangeEntry> handlesTobeAdded;
    std::vector<ChangeEntry> handlesTobeModified;

    handlesTobeAdded.push_back(newRecordHdl);

    for (auto& ids : recordHdlList)
    {
        handlesTobeAdded.push_back(ids);
    }
    if (updatedRecordHdlBmc != 0)
    {
        (bmcEventDataOps == PLDM_RECORDS_MODIFIED)
            ? handlesTobeModified.push_back(updatedRecordHdlBmc)
            : handlesTobeAdded.push_back(updatedRecordHdlBmc);
    }
    if (updatedRecordHdlHost != 0)
    {
        (hostEventDataOps == PLDM_RECORDS_MODIFIED)
            ? handlesTobeModified.push_back(updatedRecordHdlHost)
            : handlesTobeAdded.push_back(updatedRecordHdlHost);
    }
    if (!handlesTobeAdded.empty())
    {
        sendPDRRepositoryChgEventbyPDRHandles(
            std::move(handlesTobeAdded),
            std::move(std::vector<uint8_t>(1, PLDM_RECORDS_ADDED)));
    }
    if (!handlesTobeModified.empty())
    {
        sendPDRRepositoryChgEventbyPDRHandles(
            std::move(handlesTobeModified),
            std::move(std::vector<uint8_t>(1, PLDM_RECORDS_MODIFIED)));
    }
}

void FruImpl::reGenerateStatePDR(const std::string& fruObjectPath,
                                 std::vector<uint32_t>& recordHdlList)
{
    pldm::responder::pdr_utils::Type pdrType{};
    static const Json empty{};
    for (const auto& directory : statePDRJsonsDir)
    {
        for (const auto& dirEntry : fs::directory_iterator(directory))
        {
            try
            {
                if (fs::is_regular_file(dirEntry.path().string()))
                {
                    auto json = pldm::responder::pdr_utils::readJson(
                        dirEntry.path().string());
                    if (!json.empty())
                    {
                        pldm::responder::pdr_utils::DbusObjMaps tmpMap{};
                        auto effecterPDRs = json.value("effecterPDRs", empty);
                        for (const auto& effecter : effecterPDRs)
                        {
                            pdrType = effecter.value("pdrType", 0);
                            if (pdrType == PLDM_STATE_EFFECTER_PDR)
                            {
                                auto stateEffecterList = setStatePDRParams(
                                    {directory}, 0, 0, tmpMap, tmpMap, true,
                                    effecter, fruObjectPath, pdrType);
                                std::move(stateEffecterList.begin(),
                                          stateEffecterList.end(),
                                          std::back_inserter(recordHdlList));
                            }
                        }
                        auto sensorPDRs = json.value("sensorPDRs", empty);
                        for (const auto& sensor : sensorPDRs)
                        {
                            pdrType = sensor.value("pdrType", 0);
                            if (pdrType == PLDM_STATE_SENSOR_PDR)
                            {
                                auto stateSensorList = setStatePDRParams(
                                    {directory}, 0, 0, tmpMap, tmpMap, true,
                                    sensor, fruObjectPath, pdrType);
                                std::move(stateSensorList.begin(),
                                          stateSensorList.end(),
                                          std::back_inserter(recordHdlList));
                            }
                        }
                    }
                }
            }
            catch (const InternalFailure& e)
            {
                error(
                    "PDR config directory does not exist or empty, TYPE= {PDR_TYP} PATH= {DIR_PATH} ERROR={ERR_EXCEP}",
                    "PDR_TYP", (unsigned)pdrType, "DIR_PATH",
                    dirEntry.path().string(), "ERR_EXCEP", e.what());
                // log an error here
            }
            catch (const Json::exception& e)
            {
                error(
                    "Failed parsing PDR JSON file, TYPE= {PDR_TYP} ERROR={ERR_EXCEP}",
                    "PDR_TYP", (unsigned)pdrType, "ERR_EXCEP", e.what());
                // log error
            }
            catch (const std::exception& e)
            {
                error(
                    "Failed parsing PDR JSON file, TYPE= {PDR_TYP} ERROR={ERR_EXCEP}",
                    "PDR_TYP", (unsigned)pdrType, "ERR_EXCEP", e.what());
                // log appropriate error
            }
        }
    }
}

void FruImpl::getFRUTable(Response& response)
{
    auto hdrSize = response.size();
    std::vector<uint8_t> tempTable;

    if (table.size())
    {
        std::copy(table.begin(), table.end(), std::back_inserter(tempTable));
        padBytes = pldm::utils::getNumPadBytes(table.size());
        tempTable.resize(tempTable.size() + padBytes, 0);

        checksum = crc32(tempTable.data(), tempTable.size());
    }
    response.resize(hdrSize + tempTable.size() + sizeof(checksum), 0);
    std::copy(tempTable.begin(), tempTable.end(), response.begin() + hdrSize);

    // Copy the checksum to response data
    auto iter = response.begin() + hdrSize + tempTable.size();
    std::copy_n(reinterpret_cast<const uint8_t*>(&checksum), sizeof(checksum),
                iter);
}

void FruImpl::getFRURecordTableMetadata()
{
    std::vector<uint8_t> tempTable;
    if (table.size())
    {
        std::copy(table.begin(), table.end(), std::back_inserter(tempTable));
        padBytes = pldm::utils::getNumPadBytes(table.size());
        tempTable.resize(tempTable.size() + padBytes, 0);

        checksum = crc32(tempTable.data(), tempTable.size());
    }
}

int FruImpl::getFRURecordByOption(std::vector<uint8_t>& fruData,
                                  uint16_t /* fruTableHandle */,
                                  uint16_t recordSetIdentifer,
                                  uint8_t recordType, uint8_t fieldType)
{
    using sum = uint32_t;

    // FRU table is built lazily, build if not done.
    buildFRUTable();

    /* 7 is sizeof(checksum,4) + padBytesMax(3)
     * We can not know size of the record table got by options in advance, but
     * it must be less than the source table. So it's safe to use sizeof the
     * source table + 7 as the buffer length
     */
    size_t recordTableSize = table.size() - padBytes + 7;
    fruData.resize(recordTableSize, 0);

    int rc = get_fru_record_by_option_check(
        table.data(), table.size(), fruData.data(), &recordTableSize,
        recordSetIdentifer, recordType, fieldType);

    if (rc != PLDM_SUCCESS || recordTableSize == 0)
    {
        return PLDM_FRU_DATA_STRUCTURE_TABLE_UNAVAILABLE;
    }

    auto pads = pldm::utils::getNumPadBytes(recordTableSize);
    crc32(fruData.data(), recordTableSize + pads);

    auto iter = fruData.begin() + recordTableSize + pads;
    std::copy_n(reinterpret_cast<const uint8_t*>(&checksum), sizeof(checksum),
                iter);
    fruData.resize(recordTableSize + pads + sizeof(sum));

    return PLDM_SUCCESS;
}

int FruImpl::setFRUTable(const std::vector<uint8_t>& fruData)
{
    auto record =
        reinterpret_cast<const pldm_fru_record_data_format*>(fruData.data());
    if (record)
    {
        if (oemFruHandler != nullptr &&
            record->record_type == PLDM_FRU_RECORD_TYPE_OEM)
        {
            auto rc = oemFruHandler->processOEMfruRecord(fruData);
            if (!rc)
            {
                return PLDM_SUCCESS;
            }
        }
    }
    return PLDM_ERROR;
}

void FruImpl::subscribeFruPresence(
    const std::string& inventoryObjPath, const std::string& fruInterface,
    const std::string& itemInterface,
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>>& fruHotPlugMatch)
{
    static constexpr auto MAPPER_BUSNAME = "xyz.openbmc_project.ObjectMapper";
    static constexpr auto MAPPER_PATH = "/xyz/openbmc_project/object_mapper";
    static constexpr auto MAPPER_INTERFACE = "xyz.openbmc_project.ObjectMapper";

    auto& bus = pldm::utils::DBusHandler::getBus();
    try
    {
        std::vector<std::string> fruObjPaths;
        auto method = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                          MAPPER_INTERFACE, "GetSubTreePaths");
        method.append(inventoryObjPath);
        method.append(0);
        method.append(std::vector<std::string>({fruInterface}));
        auto reply = bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
        reply.read(fruObjPaths);

        for (const auto& fruObjPath : fruObjPaths)
        {
            using namespace sdbusplus::bus::match::rules;
            fruHotPlugMatch.push_back(
                std::make_unique<sdbusplus::bus::match::match>(
                    bus, propertiesChanged(fruObjPath, itemInterface),
                    [this, fruObjPath,
                     fruInterface](sdbusplus::message::message& msg) {
                DbusChangedProps props;
                std::string iface;
                msg.read(iface, props);
                processFruPresenceChange(props, fruObjPath, fruInterface);
                    }));
        }
    }
    catch (const std::exception& e)
    {
        error(
            "could not subscribe for concurrent maintenance of fru: {FRU_INTF} error {ERR_EXCEP}",
            "FRU_INTF", fruInterface, "ERR_EXCEP", e.what());
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.CMsubscribeFailure",
            pldm::PelSeverity::ERROR);
    }
}

void FruImpl::processFruPresenceChange(const DbusChangedProps& chProperties,
                                       const std::string& fruObjPath,
                                       const std::string& fruInterface)
{
    /*   std::cout << "enter processFruPresenceChange for " << fruObjPath << "
       and "
                 << fruInterface << std::endl;*/
    static constexpr auto propertyName = "Present";
    const auto it = chProperties.find(propertyName);

    if (it == chProperties.end())
    {
        return;
    }
    auto newPropVal = std::get<bool>(it->second);
    if (!isBuilt)
    {
        return;
    }

    std::vector<std::string> portObjects;
    static constexpr auto portInterface =
        "xyz.openbmc_project.Inventory.Item.Connector";

#ifdef OEM_IBM
    if (fruInterface == "xyz.openbmc_project.Inventory.Item.PCIeDevice")
    {
        if (!pldm::responder::utils::checkIfIBMCableCard(fruObjPath))
        {
            return;
        }
        pldm::responder::utils::findPortObjects(fruObjPath, portObjects);
        /*   std::cout << "after finding the port objects " <<
           portObjects.size()
                     << std::endl;*/
    }
    // as per current code the ports do not have Present property
#endif

    // if(fruInterface != "xyz.openbmc_project.Inventory.Item.PCIeDevice")
    {
        if (newPropVal)
        {
            buildIndividualFRU(fruInterface, fruObjPath);
            for (auto portObject : portObjects)
            {
                buildIndividualFRU(portInterface, portObject);
            }
        }
        else
        {
            for (auto portObject : portObjects)
            {
                removeIndividualFRU(portObject);
            }
            removeIndividualFRU(fruObjPath);
        }
    }
}

void FruImpl::sendPDRRepositoryChgEventbyPDRHandles(
    std::vector<ChangeEntry>&& pdrRecordHandles,
    std::vector<uint8_t>&& eventDataOps)
{
    uint8_t eventDataFormat = FORMAT_IS_PDR_HANDLES;
    std::vector<uint8_t> numsOfChangeEntries(1);
    std::vector<std::vector<ChangeEntry>> changeEntries(
        numsOfChangeEntries.size());
    for (auto pdrRecordHandle : pdrRecordHandles)
    {
        changeEntries[0].push_back(pdrRecordHandle);
    }
    if (changeEntries.empty())
    {
        return;
    }
    numsOfChangeEntries[0] = changeEntries[0].size();
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
        error("Failed to encode_pldm_pdr_repository_chg_event_data, rc = {RC}",
              "RC", static_cast<int>(rc));
        return;
    }
    auto instanceId = requester.getInstanceId(mctp_eid);
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
        requester.markFree(mctp_eid, instanceId);
        error("Failed to encode_platform_event_message_req, rc = {RC}", "RC",
              static_cast<unsigned>(rc));
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
                "Failed to decode_platform_event_message_resp: rc={RC}, cc={CC}",
                "RC", static_cast<int>(rc), "CC",
                static_cast<unsigned>(completionCode));
        }
    };
    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_PLATFORM, PLDM_PLATFORM_EVENT_MESSAGE,
        std::move(requestMsg), std::move(platformEventMessageResponseHandler));
    if (rc)
    {
        error(
            "Failed to send the PDR repository changed event request after CM");
    }
}

std::vector<uint32_t> FruImpl::setStatePDRParams(
    const std::vector<fs::path> pdrJsonsDir, uint16_t nextSensorId,
    uint16_t nextEffecterId,
    pldm::responder::pdr_utils::DbusObjMaps& sensorDbusObjMaps,
    pldm::responder::pdr_utils::DbusObjMaps& effecterDbusObjMaps, bool hotPlug,
    const Json& json, const std::string& fruObjectPath,
    pldm::responder::pdr_utils::Type pdrType)
{
    using namespace pldm::responder::pdr_utils;
    static DbusObjMaps& sensorDbusObjMapsRef = sensorDbusObjMaps;
    static DbusObjMaps& effecterDbusObjMapsRef = effecterDbusObjMaps;
    std::vector<uint32_t> idList;
    static const Json empty{};
    if (!hotPlug)
    {
        startStateSensorId = nextSensorId;
        startStateEffecterId = nextEffecterId;
        statePDRJsonsDir = pdrJsonsDir;
        return idList;
    }

    if (pdrType == PLDM_STATE_EFFECTER_PDR)
    {
        static const std::vector<Json> emptyList{};
        auto entries = json.value("entries", emptyList);
        for (const auto& e : entries)
        {
            size_t pdrSize = 0;
            auto effecters = e.value("effecters", emptyList);
            for (const auto& effecter : effecters)
            {
                auto set = effecter.value("set", empty);
                auto statesSize = set.value("size", 0);
                if (!statesSize)
                {
                    error(
                        "Malformed PDR JSON return pdrEntry;- no state set info, TYPE={INFO_TYP}",
                        "INFO_TYP",
                        static_cast<unsigned>(PLDM_STATE_EFFECTER_PDR));
                    throw InternalFailure();
                }
                pdrSize += sizeof(state_effecter_possible_states) -
                           sizeof(bitfield8_t) +
                           (sizeof(bitfield8_t) * statesSize);
            }
            pdrSize += sizeof(pldm_state_effecter_pdr) - sizeof(uint8_t);

            std::vector<uint8_t> entry{};
            entry.resize(pdrSize);

            pldm_state_effecter_pdr* pdr =
                reinterpret_cast<pldm_state_effecter_pdr*>(entry.data());
            if (!pdr)
            {
                error("Failed to get state effecter PDR.");
                continue;
            }
            pdr->hdr.record_handle = 0;
            pdr->hdr.version = 1;
            pdr->hdr.type = PLDM_STATE_EFFECTER_PDR;
            pdr->hdr.record_change_num = 0;
            pdr->hdr.length = pdrSize - sizeof(pldm_pdr_hdr);

            // pdr->terminus_handle = pdr::BmcPldmTerminusHandle;
            pdr->terminus_handle = TERMINUS_HANDLE;

            bool singleEffecter = false;
            try
            {
                std::string entity_path = e.value("entity_path", "");
                if (fruObjectPath.size())
                {
                    if (fruObjectPath != entity_path)
                    {
                        continue;
                    }
                    singleEffecter = true;
                }
                pdr->effecter_id =
                    startStateEffecterId++; // handler.getNextEffecterId();
                // auto& associatedEntityMap = handler.getAssociateEntityMap();
                if (entity_path != "" &&
                    associatedEntityMap.find(entity_path) !=
                        associatedEntityMap.end())
                {
                    pdr->entity_type =
                        associatedEntityMap.at(entity_path).entity_type;
                    pdr->entity_instance =
                        associatedEntityMap.at(entity_path).entity_instance_num;
                    pdr->container_id =
                        associatedEntityMap.at(entity_path).entity_container_id;
                }
                else
                {
                    pdr->entity_type = e.value("type", 0);
                    pdr->entity_instance = e.value("instance", 0);
                    pdr->container_id = e.value("container", 0);
                }
            }
            catch (const std::exception& ex)
            {
                pdr->entity_type = e.value("type", 0);
                pdr->entity_instance = e.value("instance", 0);
                pdr->container_id = e.value("container", 0);
            }
            pdr->effecter_semantic_id = 0;
            pdr->effecter_init = PLDM_NO_INIT;
            pdr->has_description_pdr = false;
            pdr->composite_effecter_count = effecters.size();

            pldm::responder::pdr_utils::DbusMappings dbusMappings{};
            pldm::responder::pdr_utils::DbusValMaps dbusValMaps{};
            uint8_t* start = entry.data() + sizeof(pldm_state_effecter_pdr) -
                             sizeof(uint8_t);
            for (const auto& effecter : effecters)
            {
                auto set = effecter.value("set", empty);
                state_effecter_possible_states* possibleStates =
                    reinterpret_cast<state_effecter_possible_states*>(start);
                possibleStates->state_set_id = set.value("id", 0);
                possibleStates->possible_states_size = set.value("size", 0);

                start += sizeof(possibleStates->state_set_id) +
                         sizeof(possibleStates->possible_states_size);
                static const std::vector<uint8_t> emptyStates{};
                pldm::responder::pdr_utils::PossibleValues stateValues;
                auto states = set.value("states", emptyStates);
                for (const auto& state : states)
                {
                    auto index = state / 8;
                    auto bit = state - (index * 8);
                    bitfield8_t* bf =
                        reinterpret_cast<bitfield8_t*>(start + index);
                    bf->byte |= 1 << bit;
                    stateValues.emplace_back(state);
                }
                start += possibleStates->possible_states_size;

                auto dbusEntry = effecter.value("dbus", empty);
                auto objectPath = dbusEntry.value("path", "");
                auto interface = dbusEntry.value("interface", "");
                auto propertyName = dbusEntry.value("property_name", "");
                auto propertyType = dbusEntry.value("property_type", "");

                pldm::responder::pdr_utils::StatestoDbusVal dbusIdToValMap{};
                pldm::utils::DBusMapping dbusMapping{};
                try
                {
                    auto service =
                        // dBusIntf.getService(objectPath.c_str(),
                        // interface.c_str());
                        pldm::utils::DBusHandler().getService(
                            objectPath.c_str(), interface.c_str());
                    dbusMapping = pldm::utils::DBusMapping{
                        objectPath, interface, propertyName, propertyType};
                    dbusIdToValMap =
                        pldm::responder::pdr_utils::populateMapping(
                            propertyType, dbusEntry["property_values"],
                            stateValues);
                }
                catch (const std::exception& e)
                {
                    error(
                        "D-Bus object path does not exist, effecter ID: {EFFECTER_ID}",
                        "EFFECTER_ID", static_cast<uint16_t>(pdr->effecter_id));
                }
                dbusMappings.emplace_back(std::move(dbusMapping));
                dbusValMaps.emplace_back(std::move(dbusIdToValMap));
            }
            /* handler.addDbusObjMaps(
                 pdr->effecter_id,
                 std::make_tuple(std::move(dbusMappings),
               std::move(dbusValMaps)));*/
            uint32_t effecterId = pdr->effecter_id;
            effecterDbusObjMapsRef.emplace(
                effecterId, std::make_tuple(std::move(dbusMappings),
                                            std::move(dbusValMaps)));
            pldm::responder::pdr_utils::PdrEntry pdrEntry{};
            pdrEntry.data = entry.data();
            pdrEntry.size = pdrSize;
            if (singleEffecter)
            {
                auto newRecordHdl = addHotPlugRecord(pdrEntry);
                // nowa dd to the vector
                idList.push_back(newRecordHdl);
            }
        }
    }
    else if (pdrType == PLDM_STATE_SENSOR_PDR)
    {
        static const std::vector<Json> emptyList{};
        auto entries = json.value("entries", emptyList);
        for (const auto& e : entries)
        {
            size_t pdrSize = 0;
            auto sensors = e.value("sensors", emptyList);
            for (const auto& sensor : sensors)
            {
                auto set = sensor.value("set", empty);
                auto statesSize = set.value("size", 0);
                if (!statesSize)
                {
                    error(
                        "Malformed PDR JSON return pdrEntry;- no state set info, TYPE={INFO_TYP}",
                        "INFO_TYP",
                        static_cast<unsigned>(PLDM_STATE_SENSOR_PDR));
                    throw InternalFailure();
                }
                pdrSize += sizeof(state_sensor_possible_states) -
                           sizeof(bitfield8_t) +
                           (sizeof(bitfield8_t) * statesSize);
            }
            pdrSize += sizeof(pldm_state_sensor_pdr) - sizeof(uint8_t);

            std::vector<uint8_t> entry{};
            entry.resize(pdrSize);

            pldm_state_sensor_pdr* pdr =
                reinterpret_cast<pldm_state_sensor_pdr*>(entry.data());
            if (!pdr)
            {
                error("Failed to get state sensor PDR.");
                continue;
            }
            pdr->hdr.record_handle = 0;
            pdr->hdr.version = 1;
            pdr->hdr.type = PLDM_STATE_SENSOR_PDR;
            pdr->hdr.record_change_num = 0;
            pdr->hdr.length = pdrSize - sizeof(pldm_pdr_hdr);

            HTOLE32(pdr->hdr.record_handle);
            HTOLE16(pdr->hdr.record_change_num);
            HTOLE16(pdr->hdr.length);

            pdr->terminus_handle = TERMINUS_HANDLE;
            bool singleSensor = false;

            try
            {
                std::string entity_path = e.value("entity_path", "");
                if (fruObjectPath.size())
                {
                    if (fruObjectPath != entity_path)
                    {
                        continue;
                    }
                    singleSensor = true;
                }
                pdr->sensor_id =
                    startStateSensorId++; // handler.getNextSensorId();
                // auto& associatedEntityMap = handler.getAssociateEntityMap();
                if (entity_path != "" &&
                    associatedEntityMap.find(entity_path) !=
                        associatedEntityMap.end())
                {
                    pdr->entity_type =
                        associatedEntityMap.at(entity_path).entity_type;
                    pdr->entity_instance =
                        associatedEntityMap.at(entity_path).entity_instance_num;
                    pdr->container_id =
                        associatedEntityMap.at(entity_path).entity_container_id;
                }
                else
                {
                    pdr->entity_type = e.value("type", 0);
                    pdr->entity_instance = e.value("instance", 0);
                    pdr->container_id = e.value("container", 0);
                }
            }
            catch (const std::exception& ex)
            {
                pdr->entity_type = e.value("type", 0);
                pdr->entity_instance = e.value("instance", 0);
                pdr->container_id = e.value("container", 0);
            }
            pdr->sensor_init = PLDM_NO_INIT;
            pdr->sensor_auxiliary_names_pdr = false;
            if (sensors.size() > 8)
            {
                throw std::runtime_error("sensor size must be less than 8");
            }
            pdr->composite_sensor_count = sensors.size();

            HTOLE16(pdr->terminus_handle);
            HTOLE16(pdr->sensor_id);
            HTOLE16(pdr->entity_type);
            HTOLE16(pdr->entity_instance);
            HTOLE16(pdr->container_id);

            pldm::responder::pdr_utils::DbusMappings dbusMappings{};
            pldm::responder::pdr_utils::DbusValMaps dbusValMaps{};
            uint8_t* start = entry.data() + sizeof(pldm_state_sensor_pdr) -
                             sizeof(uint8_t);
            for (const auto& sensor : sensors)
            {
                auto set = sensor.value("set", empty);
                state_sensor_possible_states* possibleStates =
                    reinterpret_cast<state_sensor_possible_states*>(start);
                possibleStates->state_set_id = set.value("id", 0);
                HTOLE16(possibleStates->state_set_id);
                possibleStates->possible_states_size = set.value("size", 0);

                start += sizeof(possibleStates->state_set_id) +
                         sizeof(possibleStates->possible_states_size);
                static const std::vector<uint8_t> emptyStates{};
                pldm::responder::pdr_utils::PossibleValues stateValues;
                auto states = set.value("states", emptyStates);
                for (const auto& state : states)
                {
                    auto index = state / 8;
                    auto bit = state - (index * 8);
                    bitfield8_t* bf =
                        reinterpret_cast<bitfield8_t*>(start + index);
                    bf->byte |= 1 << bit;
                    stateValues.emplace_back(state);
                }
                start += possibleStates->possible_states_size;
                auto dbusEntry = sensor.value("dbus", empty);
                auto objectPath = dbusEntry.value("path", "");
                auto interface = dbusEntry.value("interface", "");
                auto propertyName = dbusEntry.value("property_name", "");
                auto propertyType = dbusEntry.value("property_type", "");

                pldm::responder::pdr_utils::StatestoDbusVal dbusIdToValMap{};
                pldm::utils::DBusMapping dbusMapping{};
                try
                {
                    auto service =
                        // dBusIntf.getService(objectPath.c_str(),
                        // interface.c_str());
                        pldm::utils::DBusHandler().getService(
                            objectPath.c_str(), interface.c_str());
                    dbusMapping = pldm::utils::DBusMapping{
                        objectPath, interface, propertyName, propertyType};
                    dbusIdToValMap =
                        pldm::responder::pdr_utils::populateMapping(
                            propertyType, dbusEntry["property_values"],
                            stateValues);
                }
                catch (const std::exception& e)
                {
                    error(
                        "D-Bus object path does not exist, sensor ID: {SENSOR_ID}",
                        "SENSOR_ID", static_cast<uint16_t>(pdr->sensor_id));
                }
                dbusMappings.emplace_back(std::move(dbusMapping));
                dbusValMaps.emplace_back(std::move(dbusIdToValMap));
            }
            /*handler.addDbusObjMaps(
                pdr->sensor_id,
                std::make_tuple(std::move(dbusMappings),
               std::move(dbusValMaps)),
                pldm::responder::pdr_utils::TypeId::PLDM_SENSOR_ID);*/
            uint32_t sensorId = pdr->sensor_id;
            sensorDbusObjMapsRef.emplace(
                sensorId, std::make_tuple(std::move(dbusMappings),
                                          std::move(dbusValMaps)));

            // creating a match for the newly added sensor
            dbusToPLDMEventHandler->sendStateSensorEvent(sensorId,
                                                         sensorDbusObjMapsRef);

            pldm::responder::pdr_utils::PdrEntry pdrEntry{};
            pdrEntry.data = entry.data();
            pdrEntry.size = pdrSize;
            if (singleSensor)
            {
                auto newRecordHdl = addHotPlugRecord(pdrEntry);
                idList.push_back(newRecordHdl);
            }
        }
    }
    return idList;
}

uint32_t
    FruImpl::addHotPlugRecord(pldm::responder::pdr_utils::PdrEntry pdrEntry)
{
    uint32_t lastHandle = 0;
#ifdef OEM_IBM
    auto lastLocalRecord = pldm_pdr_find_last_local_record(pdrRepo);
    lastHandle = lastLocalRecord->record_handle;
#endif
    pdrEntry.handle.recordHandle = lastHandle + 1;
    return pldm_pdr_add_hotplug_record(pdrRepo, pdrEntry.data, pdrEntry.size,
                                       pdrEntry.handle.recordHandle, false,
                                       lastHandle, TERMINUS_HANDLE);
}

namespace fru
{
Response Handler::getFRURecordTableMetadata(const pldm_msg* request,
                                            size_t /*payloadLength*/)
{
    // FRU table is built lazily, build if not done.
    buildFRUTable();

    constexpr uint8_t major = 0x01;
    constexpr uint8_t minor = 0x00;
    constexpr uint32_t maxSize = 0xFFFFFFFF;

    Response response(sizeof(pldm_msg_hdr) +
                          PLDM_GET_FRU_RECORD_TABLE_METADATA_RESP_BYTES,
                      0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    impl.getFRURecordTableMetadata();

    auto rc = encode_get_fru_record_table_metadata_resp(
        request->hdr.instance_id, PLDM_SUCCESS, major, minor, maxSize,
        impl.size(), impl.numRSI(), impl.numRecords(), impl.checkSum(),
        responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    return response;
}

Response Handler::getFRURecordTable(const pldm_msg* request,
                                    size_t payloadLength)
{
    // FRU table is built lazily, build if not done.
    buildFRUTable();

    if (payloadLength != PLDM_GET_FRU_RECORD_TABLE_REQ_BYTES)
    {
        return ccOnlyResponse(request, PLDM_ERROR_INVALID_LENGTH);
    }

    Response response(
        sizeof(pldm_msg_hdr) + PLDM_GET_FRU_RECORD_TABLE_MIN_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    auto rc = encode_get_fru_record_table_resp(request->hdr.instance_id,
                                               PLDM_SUCCESS, 0,
                                               PLDM_START_AND_END, responsePtr);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    impl.getFRUTable(response);

    return response;
}

Response Handler::getFRURecordByOption(const pldm_msg* request,
                                       size_t payloadLength)
{
    if (payloadLength != sizeof(pldm_get_fru_record_by_option_req))
    {
        return ccOnlyResponse(request, PLDM_ERROR_INVALID_LENGTH);
    }

    uint32_t retDataTransferHandle{};
    uint16_t retFruTableHandle{};
    uint16_t retRecordSetIdentifier{};
    uint8_t retRecordType{};
    uint8_t retFieldType{};
    uint8_t retTransferOpFlag{};

    auto rc = decode_get_fru_record_by_option_req(
        request, payloadLength, &retDataTransferHandle, &retFruTableHandle,
        &retRecordSetIdentifier, &retRecordType, &retFieldType,
        &retTransferOpFlag);

    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    std::vector<uint8_t> fruData;
    rc = impl.getFRURecordByOption(fruData, retFruTableHandle,
                                   retRecordSetIdentifier, retRecordType,
                                   retFieldType);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    auto respPayloadLength = PLDM_GET_FRU_RECORD_BY_OPTION_MIN_RESP_BYTES +
                             fruData.size();
    Response response(sizeof(pldm_msg_hdr) + respPayloadLength, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    rc = encode_get_fru_record_by_option_resp(
        request->hdr.instance_id, PLDM_SUCCESS, 0, PLDM_START_AND_END,
        fruData.data(), fruData.size(), responsePtr, respPayloadLength);

    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    return response;
}

Response Handler::setFRURecordTable(const pldm_msg* request,
                                    size_t payloadLength)
{
    uint32_t transferHandle{};
    uint8_t transferOpFlag{};
    struct variable_field fruData;

    auto rc = decode_set_fru_record_table_req(
        request, payloadLength, &transferHandle, &transferOpFlag, &fruData);

    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    Table table(fruData.ptr, fruData.ptr + fruData.length);
    rc = impl.setFRUTable(table);
    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    Response response(sizeof(pldm_msg_hdr) +
                      PLDM_SET_FRU_RECORD_TABLE_RESP_BYTES);
    struct pldm_msg* responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    rc = encode_set_fru_record_table_resp(
        request->hdr.instance_id, PLDM_SUCCESS, 0 /* nextDataTransferHandle */,
        response.size() - sizeof(pldm_msg_hdr), responsePtr);

    if (rc != PLDM_SUCCESS)
    {
        return ccOnlyResponse(request, rc);
    }

    return response;
}

void Handler::setStatePDRParams(
    const std::vector<fs::path> pdrJsonsDir, uint16_t nextSensorId,
    uint16_t nextEffecterId,
    pldm::responder::pdr_utils::DbusObjMaps& sensorDbusObjMaps,
    pldm::responder::pdr_utils::DbusObjMaps& effecterDbusObjMaps, bool hotPlug)
{
    impl.setStatePDRParams(pdrJsonsDir, nextSensorId, nextEffecterId,
                           sensorDbusObjMaps, effecterDbusObjMaps, hotPlug,
                           Json());
}

} // namespace fru

} // namespace responder

} // namespace pldm
