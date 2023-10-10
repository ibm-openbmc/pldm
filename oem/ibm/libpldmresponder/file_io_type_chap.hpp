#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{

/** @class ChapHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  to read/write chap secret file
 */
class ChapHandler : public FileHandler
{
  public:
    /** @brief ChapHandler constructor
     */
    ChapHandler(uint32_t fileHandle, uint16_t fileType) :
        FileHandler(fileHandle), chapType(fileType)
    {}

    virtual void writeFromMemory(uint32_t /*offset*/, uint32_t length,
                                 uint64_t /*address*/,
                                 oem_platform::Handler* /*oemPlatformHandle*/,
                                 ResponseHdr& responseHdr,
                                 sdeventplus::Event& /*event*/)
    {
        FileHandler::dmaResponseToHost(responseHdr,
                                       PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, responseHdr);
    }

    virtual void readIntoMemory(uint32_t /*offset*/, uint32_t& length,
                                uint64_t /*address*/,
                                oem_platform::Handler* /*oemPlatformHandler*/,
                                ResponseHdr& responseHdr,
                                sdeventplus::Event& /*event*/)
    {
        FileHandler::dmaResponseToHost(responseHdr,
                                       PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, responseHdr);
    }

    virtual void postDataTransferCallBack(bool /*IsWriteToMemOp*/) {}

    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int write(const char* /*buffer*/, uint32_t /*offset*/,
                      uint32_t& /*length*/,
                      oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int fileAck(uint8_t /*fileStatus*/);

    virtual int newFileAvailable(uint64_t /*length*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t /*metaDataValue1*/,
                                    uint32_t /*metaDataValue2*/,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int newFileAvailableWithMetaData(uint64_t /*length*/,
                                             uint32_t /*metaDataValue1*/,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    /** @brief ChapHandler destructor
     */
    ~ChapHandler() {}

  private:
    uint16_t chapType; //!< type of the chap secret file
};
} // namespace responder
} // namespace pldm
