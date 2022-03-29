#pragma once

#include "config.h"

#include "pdr.h"

#include "license_entry.hpp"
#include "type.hpp"

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

    bool deserialize();

    dbus::SavedObjs getSavedObjs()
    {
        return savedObjs;
    }

    void setObjectPathMaps(const ObjectPathMaps& maps);

  private:
    dbus::SavedObjs savedObjs;
    fs::path filePath{PERSISTENT_FILE};
    std::map<ObjectPath, pldm_entity> entityPathMaps;
};

} // namespace serialize
} // namespace pldm
