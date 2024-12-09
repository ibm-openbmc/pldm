#include "utils.hpp"

#include <libpldm/entity.h>

#include <cstdlib>
#include <iostream>

using namespace pldm::utils;

namespace pldm
{
namespace hostbmc
{
namespace utils
{
Entities getParentEntites(const EntityAssociations& entityAssoc)
{
    Entities parents{};
    for (const auto& et : entityAssoc)
    {
        parents.push_back(et[0]);
    }

    bool found = false;
    for (auto it = parents.begin(); it != parents.end();
         it = found ? parents.erase(it) : std::next(it))
    {
        uint16_t parent_contained_id =
            pldm_entity_node_get_remote_container_id(*it);
        found = false;
        for (const auto& evs : entityAssoc)
        {
            for (size_t i = 1; i < evs.size() && !found; i++)
            {
                uint16_t node_contained_id =
                    pldm_entity_node_get_remote_container_id(evs[i]);

                pldm_entity parent_entity = pldm_entity_extract(*it);
                pldm_entity node_entity = pldm_entity_extract(evs[i]);

                if (node_entity.entity_type == parent_entity.entity_type &&
                    node_entity.entity_instance_num ==
                        parent_entity.entity_instance_num &&
                    node_contained_id == parent_contained_id)
                {
                    found = true;
                }
            }
            if (found)
            {
                break;
            }
        }
    }

    return parents;
}

void addObjectPathEntityAssociations(
    const EntityAssociations& entityAssoc, pldm_entity_node* entity,
    const fs::path& path, ObjectPathMaps& objPathMap, EntityMaps entityMaps,
    pldm::responder::oem_platform::Handler* oemPlatformHandler)
{
    if (entity == nullptr)
    {
        return;
    }

    bool found = false;
    pldm_entity node_entity = pldm_entity_extract(entity);
    if (!entityMaps.contains(node_entity.entity_type))
    {
        // entityMaps doesn't contain entity type which are not required to
        // build entity object path, so returning from here because this is a
        // expected behaviour
        return;
    }

    std::string entityName = entityMaps.at(node_entity.entity_type);
    for (const auto& ev : entityAssoc)
    {
        pldm_entity ev_entity = pldm_entity_extract(ev[0]);
        if (ev_entity.entity_instance_num == node_entity.entity_instance_num &&
            ev_entity.entity_type == node_entity.entity_type)
        {
            uint16_t node_contained_id =
                pldm_entity_node_get_remote_container_id(ev[0]);
            uint16_t entity_contained_id =
                pldm_entity_node_get_remote_container_id(entity);

            if (node_contained_id != entity_contained_id)
            {
                continue;
            }

            fs::path p =
                path /
                fs::path{entityName +
                         std::to_string(node_entity.entity_instance_num)};
            std::string entity_path = p.string();
            if (oemPlatformHandler)
            {
                oemPlatformHandler->updateOemDbusPaths(entity_path);
            }
            try
            {
                pldm::utils::DBusHandler().getService(entity_path.c_str(),
                                                      nullptr);
                // If the entity obtained from the remote PLDM terminal is not
                // in the MAP, or there is no auxiliary name PDR, add it
                // directly. Otherwise, check whether the DBus service of
                // entity_path exists, and overwrite the entity if it does not
                // exist.
                if (objPathMap.contains(entity_path))
                {
                    objPathMap[entity_path] = node_entity;
                }
            }
            catch (const std::exception&)
            {
                objPathMap[entity_path] = node_entity;
            }

            for (size_t i = 1; i < ev.size(); i++)
            {
                addObjectPathEntityAssociations(entityAssoc, ev[i], p,
                                                objPathMap, entityMaps,
                                                oemPlatformHandler);
            }
            found = true;
        }
    }

    if (!found)
    {
        std::string dbusPath =
            path / fs::path{entityName +
                            std::to_string(node_entity.entity_instance_num)};
        if (oemPlatformHandler)
        {
            oemPlatformHandler->updateOemDbusPaths(dbusPath);
        }
        try
        {
            pldm::utils::DBusHandler().getService(dbusPath.c_str(), nullptr);
            if (objPathMap.contains(dbusPath))
            {
                objPathMap[dbusPath] = node_entity;
            }
        }
        catch (const std::exception&)
        {
            objPathMap[dbusPath] = node_entity;
        }
    }
}

void updateEntityAssociation(
    const EntityAssociations& entityAssoc,
    pldm_entity_association_tree* entityTree, ObjectPathMaps& objPathMap,
    EntityMaps entityMaps,
    pldm::responder::oem_platform::Handler* oemPlatformHandler)
{
    std::vector<pldm_entity_node*> parentsEntity =
        getParentEntites(entityAssoc);
    for (const auto& entity : parentsEntity)
    {
        fs::path path{"/xyz/openbmc_project/inventory"};
        std::deque<std::string> paths{};
        pldm_entity node_entity = pldm_entity_extract(entity);
        uint16_t remoteContainerId =
            pldm_entity_node_get_remote_container_id(entity);
        /* If the logical bit is set in the host containerId, cosider this as
         * host entity and find in host node by setting the is_remote to true)
         */
        info(
            "Update Entity Association , type = {ENTITY_TYP}, num = {ENTITY_NUM}, container = {CONT}, Remote Container ID = {RID}, is remote = {REMOTE}",
            "ENTITY_TYP", static_cast<int>(node_entity.entity_type),
            "ENTITY_NUM", static_cast<int>(node_entity.entity_instance_num),
            "CONT", static_cast<int>(node_entity.entity_container_id), "RID",
            remoteContainerId, "REMOTE", (bool)(remoteContainerId & 0x8000));

        auto node = pldm_entity_association_tree_find_with_locality(
            entityTree, &node_entity, (remoteContainerId & 0x8000));

        if (!node)
        {
            continue;
        }

        bool found = true;
        while (node)
        {
            if (!pldm_entity_is_exist_parent(node))
            {
                break;
            }

            pldm_entity parent = pldm_entity_get_parent(node);
            try
            {
                paths.push_back(entityMaps.at(parent.entity_type) +
                                std::to_string(parent.entity_instance_num));
            }
            catch (const std::exception& e)
            {
                lg2::error(
                    "Parent entity not found in the entityMaps, type: {ENTITY_TYPE}, num: {NUM}, e: {ERROR}",
                    "ENTITY_TYPE", (int)parent.entity_type, "NUM",
                    (int)parent.entity_instance_num, "ERROR", e);
                found = false;
                break;
            }

            node = pldm_entity_association_tree_find_with_locality(
                entityTree, &parent, false);
        }

        if (!found)
        {
            continue;
        }

        while (!paths.empty())
        {
            path = path / fs::path{paths.back()};
            paths.pop_back();
        }

        addObjectPathEntityAssociations(entityAssoc, entity, path, objPathMap,
                                        entityMaps, oemPlatformHandler);
    }
}

EntityMaps parseEntityMap(const fs::path& filePath)
{
    const Json emptyJson{};
    EntityMaps entityMaps{};
    std::ifstream jsonFile(filePath);
    auto data = Json::parse(jsonFile);
    if (data.is_discarded())
    {
        error("Failed parsing of EntityMap data from json file: '{JSON_PATH}'",
              "JSON_PATH", filePath);
        return entityMaps;
    }
    auto entities = data.value("EntityTypeToDbusStringMap", emptyJson);
    char* err;
    try
    {
        std::ranges::transform(
            entities.items(), std::inserter(entityMaps, entityMaps.begin()),
            [&err](const auto& element) {
                std::string key = static_cast<EntityName>(element.key());
                return std::make_pair(strtol(key.c_str(), &err, 10),
                                      static_cast<EntityName>(element.value()));
            });
    }
    catch (const std::exception& e)
    {
        error(
            "Failed to create entity to DBus string mapping {ERROR} and Conversion failure is '{ERR}'",
            "ERROR", e, "ERR", err);
    }

    return entityMaps;
}

std::shared_ptr<EntityAuxiliaryNames>
    parseEntityAuxNamesPDR(std::vector<uint8_t>& pdrData)
{
    auto names_offset = sizeof(struct pldm_pdr_hdr) +
                        PLDM_PDR_ENTITY_AUXILIARY_NAME_PDR_MIN_LENGTH;
    auto names_size = pdrData.size() - names_offset;

    size_t decodedPdrSize =
        sizeof(struct pldm_entity_auxiliary_names_pdr) + names_size;
    auto vPdr = std::vector<char>(decodedPdrSize);
    auto decodedPdr =
        reinterpret_cast<struct pldm_entity_auxiliary_names_pdr*>(vPdr.data());

    auto rc = decode_entity_auxiliary_names_pdr(pdrData.data(), pdrData.size(),
                                                decodedPdr, decodedPdrSize);
    if (rc)
    {
        error("Failed to decode Entity Auxiliary Name PDR data, error {RC}.",
              "RC", rc);
        return nullptr;
    }

    auto vNames =
        std::vector<pldm_entity_auxiliary_name>(decodedPdr->name_string_count);
    decodedPdr->names = vNames.data();

    rc = decode_pldm_entity_auxiliary_names_pdr_index(decodedPdr);
    if (rc)
    {
        error("Failed to decode Entity Auxiliary Name, error {RC}.", "RC", rc);
        return nullptr;
    }

    AuxiliaryNames nameStrings{};
    for (const auto& count :
         std::views::iota(0, static_cast<int>(decodedPdr->name_string_count)))
    {
        std::string_view nameLanguageTag =
            static_cast<std::string_view>(decodedPdr->names[count].tag);
        const size_t u16NameStringLen =
            std::char_traits<char16_t>::length(decodedPdr->names[count].name);
        std::u16string u16NameString(decodedPdr->names[count].name,
                                     u16NameStringLen);
        std::transform(u16NameString.cbegin(), u16NameString.cend(),
                       u16NameString.begin(),
                       [](uint16_t utf16) { return be16toh(utf16); });
        std::string nameString =
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}
                .to_bytes(u16NameString);
        nameStrings.emplace_back(std::make_pair(nameLanguageTag, nameString));
    }

    EntityKey key{decodedPdr->container.entity_type,
                  decodedPdr->container.entity_instance_num,
                  decodedPdr->container.entity_container_id};

    return std::make_shared<EntityAuxiliaryNames>(key, nameStrings);
}

} // namespace utils
} // namespace hostbmc
} // namespace pldm
