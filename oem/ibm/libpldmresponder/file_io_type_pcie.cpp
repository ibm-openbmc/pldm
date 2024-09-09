#include "file_io_type_pcie.hpp"

#include "libpldm/base.h"

#include <libpldm/oem/ibm/file_io.h>
#include <stdint.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{

static constexpr auto pciePath = "/var/lib/pldm/pcie-topology/";
constexpr auto topologyFile = "topology";
constexpr auto cableInfoFile = "cableinfo";

namespace fs = std::filesystem;
std::unordered_map<uint16_t, bool> PCIeInfoHandler::receivedFiles;

PCIeInfoHandler::PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType) :
    FileHandler(fileHandle), infoType(fileType)
{
    receivedFiles.emplace(infoType, false);
}

void PCIeInfoHandler::writeFromMemory(
    uint32_t offset, uint32_t length, uint64_t address,
    oem_platform::Handler* /*oemPlatformHandler*/,
    SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event)
{
    if (!fs::exists(pciePath))
    {
        fs::create_directories(pciePath);
        fs::permissions(pciePath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if (infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
        infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    try
    {
        std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary);
        if (!pcieData)
        {
            error("PCIe Info file creation error ");
            FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                     PLDM_ERROR, 0);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
            return;
        }
        transferFileData(infoFile, false, offset, length, address,
                         sharedAIORespDataobj, event);
    }
    catch (const std::exception& e)
    {
        FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                 PLDM_ERROR, 0);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        error("Create/Write data to the File type '{TYPE}' failed with {ERROR}",
              "TYPE", (int)infoType, "ERROR", e);
        return;
    }
}

int PCIeInfoHandler::write(const char* buffer, uint32_t, uint32_t& length,
                           oem_platform::Handler* /*oemPlatformHandler*/)
{
    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if (infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
        infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    try
    {
        std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary |
                                             std::ios::app);

        if (!buffer)
        {
            pcieData.write(buffer, length);
        }
        pcieData.close();
    }
    catch (const std::exception& e)
    {
        error("Create/Write data to the File type {TYPE}, failed {ERROR}",
              "TYPE", (int)infoType, "ERROR", e);
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int PCIeInfoHandler::fileAck(uint8_t /*fileStatus*/)
{
    receivedFiles[infoType] = true;
    try
    {
        if (receivedFiles.at(PLDM_FILE_TYPE_CABLE_INFO) &&
            receivedFiles.at(PLDM_FILE_TYPE_PCIE_TOPOLOGY))
        {
            receivedFiles.clear();
        }
    }
    catch (const std::out_of_range& e)
    {
        info("Received only one of the topology file");
    }
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
