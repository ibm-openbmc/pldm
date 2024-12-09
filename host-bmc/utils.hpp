#pragma once

#include "common/utils.hpp"
#include "libpldmresponder/oem_handler.hpp"

#include <libpldm/pdr.h>

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

PHOSPHOR_LOG2_USING;

using namespace pldm::utils;
namespace pldm
{
namespace hostbmc
{
namespace utils
{

using ContainerID = uint16_t;
using EntityInstanceNumber = uint16_t;
using EntityType = uint16_t;

/** @struct EntityKey
 *
 *  EntityKey uniquely identifies the PLDM entity and a combination of Entity
 *  Type, Entity Instance Number, Entity Container ID
 *
 */
struct EntityKey
{
    EntityType type;                  //!< Entity type
    EntityInstanceNumber instanceIdx; //!< Entity instance number
    ContainerID containerId;          //!< Entity container ID

    bool operator==(const EntityKey& e) const
    {
        return ((type == e.type) && (instanceIdx == e.instanceIdx) &&
                (containerId == e.containerId));
    }
};

using NameLanguageTag = std::string;
using AuxiliaryNames = std::vector<std::pair<NameLanguageTag, std::string>>;
using EntityKey = struct EntityKey;
using EntityAuxiliaryNames = std::tuple<EntityKey, AuxiliaryNames>;

/** @brief Vector a entity name to pldm_entity from entity association tree
 *  @param[in]  entityAssoc    - Vector of associated pldm entities
 *  @param[in]  entityTree     - entity association tree
 *  @param[out] objPathMap     - maps an object path to pldm_entity from the
 *                               BMC's entity association tree
 *  @return
 */
void updateEntityAssociation(
    const pldm::utils::EntityAssociations& entityAssoc,
    pldm_entity_association_tree* entityTree,
    pldm::utils::ObjectPathMaps& objPathMap, pldm::utils::EntityMaps entityMaps,
    pldm::responder::oem_platform::Handler* oemPlatformHandler);

/** @brief Parsing entity to DBus string mapping from json file
 *
 *  @param[in]  filePath - JSON file path for parsing
 *
 *  @return returns the entity to DBus string mapping object
 */
pldm::utils::EntityMaps parseEntityMap(const fs::path& filePath);

/** @brief Parse the enitity auxilliary names PDRs
 *
 *  @param[in] pdrData - the response PDRs from GetPDR command
 *
 *  @return - EntityAuxiliaryNames details
 */
std::shared_ptr<EntityAuxiliaryNames>
    parseEntityAuxNamesPDR(std::vector<uint8_t>& pdrData);

} // namespace utils
} // namespace hostbmc
} // namespace pldm
