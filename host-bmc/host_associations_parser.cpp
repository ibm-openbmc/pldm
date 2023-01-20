#include "host_associations_parser.hpp"

#include <xyz/openbmc_project/Common/error.hpp>

#include <fstream>
#include <iostream>

using namespace pldm::utils;

namespace pldm
{
namespace host_associations
{
using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

constexpr auto hostfruAssociationsJson = "host_fru_associations.json";

void HostAssociationsParser::parseHostAssociations(const std::string& jsonPath)
{
    fs::path jsonDir(jsonPath);
    if (!fs::exists(jsonDir) || fs::is_empty(jsonDir))
    {
        std::cerr << "Host Associations json path does not exist, DIR="
                  << jsonPath << "\n";
        return;
    }

    fs::path jsonFilePath = jsonDir / hostfruAssociationsJson;

    if (!fs::exists(jsonFilePath))
    {
        std::cerr << "json does not exist, PATH=" << jsonFilePath << "\n";
        throw InternalFailure();
    }

    std::ifstream jsonFile(jsonFilePath);
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        std::cerr << "Parsing json file failed, FILE=" << jsonFilePath << "\n";
        throw InternalFailure();
    }
    const Json empty{};
    const std::vector<Json> emptyList{};

    auto entries = data.value("associations", emptyList);
    for (const auto& entry : entries)
    {
        pldm::pdr::EntityType from_entity_type =
            entry.value("from_entity_type", 0);
        pldm::pdr::EntityType to_entity_type = entry.value("to_entity_type", 0);
        std::string forward_association =
            entry.value("forward_association", "");
        std::string reverse_assocation = entry.value("reverse_association", "");
        associationsInfoMap[std::make_pair(from_entity_type, to_entity_type)] =
            std::make_pair(forward_association, reverse_assocation);
    }
}

} // namespace host_associations
} // namespace pldm
