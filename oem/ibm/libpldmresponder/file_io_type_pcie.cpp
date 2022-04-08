#include "file_io_type_pcie.hpp"

#include "libpldm/base.h"
#include "oem/ibm/libpldm/file_io.h"

#include "common/utils.hpp"

#include <stdint.h>

#include <iostream>

namespace pldm
{
namespace responder
{

static constexpr auto pciePath = "/var/lib/pldm/pcie-topology/";
constexpr auto topologyFile = "topology";
constexpr auto cableInfoFile = "cableinfo";

namespace fs = std::filesystem;


int PCIeInfoHandler::writeFromMemory(
    uint32_t offset, uint32_t length, uint64_t address,
    oem_platform::Handler* /*oemPlatformHandler*/)
{
    if (!fs::exists(pciePath))
    {
        fs::create_directories(pciePath);
        fs::permissions(pciePath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if(infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
    	infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary);
    if (!pcieData)
    {
        std::cerr << "PCIe Info file creation error " << std::endl;
        return -1;
    }

    auto rc =
        transferFileData(infoFile, false, offset, length, address);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "transferFileData failed with rc= " << rc << " \n";
        return rc;
    }

    return PLDM_SUCCESS;
}

int PCIeInfoHandler::write(const char* buffer, uint32_t, uint32_t& length,
                       oem_platform::Handler* /*oemPlatformHandler*/)
{
    int rc = PLDM_SUCCESS;

    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if(infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
    	infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary | std::ios::app);
    if (!pcieData)
    {
        std::cerr << "PCIe Info file creation error: " << infoFile 
                  << std::endl;
        return -1;
    }

    if (buffer != nullptr)
    {
        pcieData.write(buffer, length);
    }
    pcieData.close();

    return rc;
}


int PCIeInfoHandler::fileAck(uint8_t fileStatus)
{
    // Transfer complete. Parse the data
    return PLDM_SUCCESS;	 
}

int PCIeInfoHandler::newFileAvailable(uint64_t)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}


int PCIeInfoHandler::readIntoMemory(uint32_t , uint32_t& ,
                                uint64_t ,
                                oem_platform::Handler* /*oemPlatformHandler*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::read(uint32_t , uint32_t& , Response& ,
                      oem_platform::Handler* /*oemPlatformHandler*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::fileAckWithMetaData(uint8_t /*fileStatus*/,
                                     uint32_t /*metaDataValue1*/,
                                     uint32_t /*metaDataValue2*/,
                                     uint32_t /*metaDataValue3*/,
                                     uint32_t /*metaDataValue4*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::newFileAvailableWithMetaData(uint64_t /*length*/,
                                              uint32_t /*metaDataValue1*/,
                                              uint32_t /*metaDataValue2*/,
                                              uint32_t /*metaDataValue3*/,
                                              uint32_t /*metaDataValue4*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

} // namespace responder
} // namespace pldm
