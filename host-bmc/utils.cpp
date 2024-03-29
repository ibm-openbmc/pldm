#include "common/utils.hpp"

#include "libpldm/entity.h"

#include "common/utils.hpp"
#include "utils.hpp"

#include <phosphor-logging/lg2.hpp>

#include <iostream>

PHOSPHOR_LOG2_USING;

namespace fs = std::filesystem;
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

    for (auto it = parents.begin(); it != parents.end();)
    {
        uint16_t parent_host_contanied_id = pldm_extract_host_container_id(*it);
        bool find = false;
        for (const auto& evs : entityAssoc)
        {
            for (size_t i = 1; i < evs.size(); i++)
            {
                uint16_t node_host_contanied_id =
                    pldm_extract_host_container_id(evs[i]);

                pldm_entity parent_entity = pldm_entity_extract(*it);
                pldm_entity node_entity = pldm_entity_extract(evs[i]);

                if (node_entity.entity_type == parent_entity.entity_type &&
                    node_entity.entity_instance_num ==
                        parent_entity.entity_instance_num &&
                    node_host_contanied_id == parent_host_contanied_id)
                {
                    find = true;
                    break;
                }
            }
            if (find)
            {
                break;
            }
        }
        if (find)
        {
            it = parents.erase(it);
        }
        else
        {
            it++;
        }
    }

    return parents;
}

void addObjectPathEntityAssociations(
    const EntityAssociations& entityAssoc, pldm_entity_node* entity,
    const ObjectPath& path, ObjectPathMaps& objPathMap,
    pldm::responder::oem_platform::Handler* oemPlatformHandler)
{
    if (entity == nullptr)
    {
        return;
    }

    bool find = false;
    pldm_entity node_entity = pldm_entity_extract(entity);
    if (!entityMaps.contains(node_entity.entity_type))
    {
        return;
    }

    std::string entityName = entityMaps.at(node_entity.entity_type);
    for (auto& ev : entityAssoc)
    {
        pldm_entity ev_entity = pldm_entity_extract(ev[0]);
        if (ev_entity.entity_instance_num == node_entity.entity_instance_num &&
            ev_entity.entity_type == node_entity.entity_type)
        {
            uint16_t node_host_contanied_id =
                pldm_extract_host_container_id(ev[0]);
            uint16_t entity_host_contanied_id =
                pldm_extract_host_container_id(entity);

            if (node_host_contanied_id != entity_host_contanied_id)
            {
                continue;
            }

            ObjectPath p =
                path /
                fs::path{entityName +
                         std::to_string(node_entity.entity_instance_num)};
            std::string entity_path = p.string();
            if (oemPlatformHandler != nullptr)
            {
                oemPlatformHandler->upadteOemDbusPaths(entity_path);
            }
            try
            {
                pldm::utils::DBusHandler().getService(entity_path.c_str(),
                                                      nullptr);
                if (objPathMap.contains(entity_path))
                {
                    // if the object is from PLDM, them update/refresh the
                    // object map as the map would be with junk values after a
                    // power off
                    objPathMap[entity_path] = entity;
                }
            }
            catch (const std::exception&)
            {
                objPathMap[entity_path] = entity;
            }

            for (size_t i = 1; i < ev.size(); i++)
            {
                addObjectPathEntityAssociations(entityAssoc, ev[i], p,
                                                objPathMap, oemPlatformHandler);
            }
            find = true;
        }
    }

    if (!find)
    {
        std::string dbusPath =
            path / fs::path{entityName +
                            std::to_string(node_entity.entity_instance_num)};

        if (oemPlatformHandler != nullptr)
        {
            oemPlatformHandler->upadteOemDbusPaths(dbusPath);
        }
        try
        {
            pldm::utils::DBusHandler().getService(dbusPath.c_str(), nullptr);
            if (objPathMap.contains(dbusPath))
            {
                // if the object is from PLDM, them update/refresh the object
                // map as the map would be with junk values after a power off
                objPathMap[dbusPath] = entity;
            }
        }
        catch (const std::exception&)
        {
            objPathMap[dbusPath] = entity;
        }
    }
}

void updateEntityAssociation(
    const EntityAssociations& entityAssoc,
    pldm_entity_association_tree* entityTree, ObjectPathMaps& objPathMap,
    pldm::responder::oem_platform::Handler* oemPlatformHandler)
{
    std::vector<pldm_entity_node*> parentsEntity =
        getParentEntites(entityAssoc);
    for (auto& entity : parentsEntity)
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

        auto node = pldm_entity_association_tree_find(
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
                error(
                    "Parent entity not found in the entityMaps, type = {ENTITY_TYP}, num = {ENTITY_NUM}, e = {ERR_EXCEP}",
                    "ENTITY_TYP", static_cast<int>(parent.entity_type),
                    "ENTITY_NUM", static_cast<int>(parent.entity_instance_num),
                    "ERR_EXCEP", e.what());
                found = false;
                break;
            }

            node = pldm_entity_association_tree_find(entityTree, &parent,
                                                     false);
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
                                        oemPlatformHandler);
    }
}
void setCoreCount(const EntityAssociations& Associations)
{
    static constexpr auto searchpath = "/xyz/openbmc_project/";
    int depth = 0;
    std::vector<std::string> cpuInterface = {
        "xyz.openbmc_project.Inventory.Item.Cpu"};
    pldm::utils::GetSubTreeResponse response =
        pldm::utils::DBusHandler().getSubtree(searchpath, depth, cpuInterface);

    // get the CPU pldm entities
    for (const auto& entries : Associations)
    {
        auto parent = pldm_entity_extract(entries[0]);
        // entries[0] would be the parent in the entity association map
        if (parent.entity_type == PLDM_ENTITY_PROC)
        {
            int corecount = 0;
            for (const auto& entry : entries)
            {
                auto child = pldm_entity_extract(entry);
                if (child.entity_type == (PLDM_ENTITY_PROC | 0x8000))
                {
                    // got a core child
                    ++corecount;
                }
            }

            auto grand_parent = pldm_entity_get_parent(entries[0]);
            std::string grepWord =
                entityMaps.at(grand_parent.entity_type) +
                std::to_string(grand_parent.entity_instance_num) + "/" +
                entityMaps.at(parent.entity_type) +
                std::to_string(parent.entity_instance_num);
            for (const auto& [objectPath, serviceMap] : response)
            {
                // find the object path with first occurance of coreX
                if (objectPath.find(grepWord) != std::string::npos)
                {
                    pldm::utils::DBusMapping dbusMapping{
                        objectPath, cpuInterface[0], "CoreCount", "uint16_t"};
                    pldm::utils::PropertyValue value =
                        static_cast<uint16_t>(corecount);
                    try
                    {
                        pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                                   value);
                    }
                    catch (const std::exception& e)
                    {
                        error(
                            "failed to set the core count property ERROR={ERR_EXCEP}",
                            "ERR_EXCEP", e.what());
                    }
                }
            }
        }
    }
}
} // namespace utils
} // namespace hostbmc
} // namespace pldm
