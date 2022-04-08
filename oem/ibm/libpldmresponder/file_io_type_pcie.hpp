#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{

/** @class PCIeInfoHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  handle the pcie topology file and cable information from host to the bmc
 */
class PCIeInfoHandler : public FileHandler
{
  public:
    /** @brief PCIeInfoHandler constructor
     */
    PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType) :
        FileHandler(fileHandle), infoType(fileType)
    {}

    virtual int writeFromMemory(uint32_t offset, uint32_t length,
                                uint64_t address,
                                oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int readIntoMemory(uint32_t offset, uint32_t& length,
                               uint64_t address,
                               oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAck(uint8_t fileStatus);

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t metaDataValue1,
                                    uint32_t metaDataValue2,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/);

    virtual int newFileAvailableWithMetaData(uint64_t length,
                                             uint32_t metaDataValue1,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/);

    /** @brief PCIeInfoHandler destructor
     */
    ~PCIeInfoHandler()
    {}

  private:
    uint16_t infoType; //!< type of the information

};

} // namespace responder
} // namespace pldm
