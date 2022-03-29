#include "serialize.hpp"

#include "type.hpp"

#include <cereal/archives/json.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

// Register class version with Cereal
CEREAL_CLASS_VERSION(pldm::serialize::Serialize, 1)

namespace pldm
{
namespace serialize
{

namespace fs = std::filesystem;

void Serialize::serialize(const std::string& path, const std::string& intf,
                          const std::string& name, dbus::PropertyValue value)
{
    if (path.empty() || intf.empty())
    {
        return;
    }

    if (!entityPathMaps.contains(path))
    {
        return;
    }

    uint16_t type = entityPathMaps[path].entity_type;
    uint16_t num = entityPathMaps[path].entity_instance_num;
    uint16_t cid = entityPathMaps[path].entity_container_id;

    if (!savedObjs.contains(type) || !savedObjs[type].contains(path))
    {
        std::map<std::string, std::map<std::string, pldm::dbus::PropertyValue>>
            maps{{{intf, {{name, value}}}}};
        savedObjs[type][path] = std::make_tuple(num, cid, maps);
    }
    else
    {
        auto& [num, cid, objs] = savedObjs[type][path];
        objs[intf][name] = value;
    }

    auto dir = filePath.parent_path();
    if (!fs::exists(dir))
    {
        fs::create_directories(dir);
    }

    std::ofstream os(filePath.c_str(), std::ios::binary);
    cereal::JSONOutputArchive oarchive(os);
    oarchive(savedObjs);
}

bool Serialize::deserialize()
{
    if (!fs::exists(filePath))
    {
        std::cerr << "File does not exist, FILE_PATH = " << filePath
                  << std::endl;
        return false;
    }

    try
    {
        savedObjs.clear();
        std::ifstream is(filePath.c_str(), std::ios::in | std::ios::binary);
        cereal::JSONInputArchive iarchive(is);
        iarchive(savedObjs);

        return true;
    }
    catch (const cereal::Exception& e)
    {
        std::cerr << "Failed to restore groups, ERROR = " << e.what()
                  << std::endl;
        fs::remove(filePath);
    }

    return false;
}

void Serialize::setObjectPathMaps(const ObjectPathMaps& maps)
{
    for (const auto& [objpath, nodeentity] : maps)
    {
        pldm_entity entity = pldm_entity_extract(nodeentity);
        entityPathMaps.emplace(objpath, entity);
    }
}

} // namespace serialize
} // namespace pldm
