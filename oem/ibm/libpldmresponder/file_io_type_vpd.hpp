#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{
using vpdFileType = uint16_t;
/** @class keywordFileHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  to read #D keyword file
 */
class keywordHandler : public FileHandler
{
  public:
    /** @brief Handler constructor
     */
    keywordHandler(uint32_t fileHandle, uint16_t /* fileType */) :
        FileHandler(fileHandle)
    {}
    virtual void writeFromMemory(uint32_t /*offset*/, uint32_t length,
                                 uint64_t /*address*/,
                                 oem_platform::Handler* /*oemPlatformHandler*/,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& /*event*/)
    {
        FileHandler::dmaResponseToRemoteTerminus(
            sharedAIORespDataobj, PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
    }
    virtual void readIntoMemory(uint32_t /*offset*/, uint32_t length,
                                uint64_t /*address*/,
                                oem_platform::Handler* /*oemPlatformHandler*/,
                                SharedAIORespData& sharedAIORespDataobj,
                                sdeventplus::Event& /*event*/)
    {
        FileHandler::dmaResponseToRemoteTerminus(
            sharedAIORespDataobj, PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
    }
    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);
    virtual int write(const char* /*buffer*/, uint32_t /*offset*/,
                      uint32_t& /*length*/,
                      oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    virtual int fileAck(uint8_t /*fileStatus*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    virtual int newFileAvailable(uint64_t /*length*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    virtual int fileAckWithMetaData(
        uint8_t /*fileStatus*/, uint32_t /*metaDataValue1*/,
        uint32_t /*metaDataValue2*/, uint32_t /*metaDataValue3*/,
        uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    virtual int newFileAvailableWithMetaData(
        uint64_t /*length*/, uint32_t /*metaDataValue1*/,
        uint32_t /*metaDataValue2*/, uint32_t /*metaDataValue3*/,
        uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    /** @brief Method to do necessary operation according different
     *         file type and being call when data transfer completed.
     *
     *  @param[in] IsWriteToMemOp - type of operation to decide what operation
     *                              needs to be done after data transfer.
     */
    virtual int postDataTransferCallBack(bool /*IsWriteToMemOp*/,
                                         uint32_t /*length*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    /** @brief keywordHandler destructor
     */
    ~keywordHandler() {}
};
} // namespace responder
} // namespace pldm
