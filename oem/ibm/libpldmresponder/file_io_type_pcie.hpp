#pragma once

#include "file_io_by_type.hpp"

#include <unordered_map>

namespace pldm
{
namespace responder
{

/** @class PCIeInfoHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used to handle the
 *  pcie topology file and cable information from remote PLDM terminus to the
 *  bmc
 */
class PCIeInfoHandler : public FileHandler
{
  public:
    /** @brief PCIeInfoHandler constructor
     */
    PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType);

    virtual void writeFromMemory(uint32_t offset, uint32_t length,
                                 uint64_t address,
                                 oem_platform::Handler* /*oemPlatformHandler*/,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& event);

    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/,
                      struct fileack_status_metadata& /*metaDataObj*/);

    virtual int fileAck(uint8_t fileStatus);

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

    virtual int read(uint32_t /*offset*/, uint32_t& /*length*/,
                     Response& /*response*/,
                     oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

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

    virtual int postDataTransferCallBack(bool /*IsWriteToMemOp*/,
                                         uint32_t /*length*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual void postWriteAction(
        const uint16_t /*fileType*/, const uint32_t /*fileHandle*/,
        const struct fileack_status_metadata& /*metaDataObj*/) {};

    /** @brief PCIeInfoHandler destructor
     */
    ~PCIeInfoHandler() {}

  private:
    uint16_t infoType; //!< type of the information

    /** @brief A static unordered map storing information about received files.
     *
     *  This unordered map associates file type with a boolean value indicating
     *  whether the file of that type has been received or not.
     */
    static std::unordered_map<uint16_t, bool> receivedFiles;
};

} // namespace responder
} // namespace pldm
