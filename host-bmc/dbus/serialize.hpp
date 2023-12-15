#pragma once

#include "license_entry.hpp"
#include "type.hpp"

#include <libpldm/pdr.h>

#include <filesystem>
#include <fstream>

namespace pldm
{
namespace serialize
{
namespace fs = std::filesystem;

using ObjectPath = fs::path;
using ObjectPathMaps = std::map<ObjectPath, pldm_entity_node*>;

/** @class Serialize
 *  @brief Store and restore
 */
class Serialize
{
  private:
    Serialize()
    {
        deserialize();
    }

  public:
    Serialize(const Serialize&) = delete;
    Serialize(Serialize&&) = delete;
    Serialize& operator=(const Serialize&) = delete;
    Serialize& operator=(Serialize&&) = delete;
    ~Serialize() = default;

    static Serialize& getSerialize()
    {
        static Serialize serialize;
        return serialize;
    }

    void serialize(const std::string& path, const std::string& name,
                   const std::string& type = "",
                   dbus::PropertyValue value = {});

    /** @brief Save the map of Key Value
     *
     *  @param[in] key
     *  @param[in] Property value
     *  @return void
     */
    void serializeKeyVal(const std::string& key, dbus::PropertyValue value);

    bool deserialize();

    dbus::SavedObjs getSavedObjs()
    {
        return savedObjs;
    }

    std::map<std::string, pldm::dbus::PropertyValue> getSavedKeyVals()
    {
        return savedKeyVal;
    }

    void setObjectPathMaps(const ObjectPathMaps& maps);

    void deleteObjsFromType(uint16_t type);

    void reSerialize(const std::vector<uint16_t> types);

    void setEntityTypes(const std::set<uint16_t>& storeEntities);

  private:
    dbus::SavedObjs savedObjs;
    fs::path filePath{PERSISTENT_FILE};
    std::set<uint16_t> storeEntityTypes;
    std::map<ObjectPath, pldm_entity> entityPathMaps;
    std::map<std::string, pldm::dbus::PropertyValue> savedKeyVal;
};

} // namespace serialize
} // namespace pldm
