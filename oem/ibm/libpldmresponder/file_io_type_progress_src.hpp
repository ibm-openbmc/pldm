#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{

namespace responder
{

/** @class ProgressCodeHandler
 *
 * @brief Inherits and implemented FileHandler. This class is used
 * to read the Progress SRC's from the Host.
 */
class ProgressCodeHandler : public FileHandler
{
  public:
    /** @brief ProgressCodeHandler constructor
     */
    ProgressCodeHandler(uint32_t fileHandle) : FileHandler(fileHandle) {}

    void writeFromMemory(uint32_t /*offset*/, uint32_t length,
                         uint64_t /*address*/,
                         oem_platform::Handler* /*oemPlatformHandler*/,
                         SharedAIORespData& sharedAIORespDataobj,
                         sdeventplus::Event& /*event*/) override
    {
        FileHandler::dmaResponseToRemoteTerminus(
            sharedAIORespDataobj, PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
    }

    int write(const char* buffer, uint32_t offset, uint32_t& length,
              oem_platform::Handler* oemPlatformHandler) override;

    void readIntoMemory(uint32_t /*offset*/, uint32_t length,
                        uint64_t /*address*/,
                        oem_platform::Handler* /*oemPlatformHandler*/,
                        SharedAIORespData& sharedAIORespDataobj,
                        sdeventplus::Event& /*event*/) override
    {
        FileHandler::dmaResponseToRemoteTerminus(
            sharedAIORespDataobj, PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
    }

    int read(uint32_t /*offset*/, uint32_t& /*length*/, Response& /*response*/,
             oem_platform::Handler* /*oemPlatformHandler*/) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    int fileAck(uint8_t /*fileStatus*/) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    int newFileAvailable(uint64_t /*length*/) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int newFileAvailableWithMetaData(uint64_t /*length*/,
                                             uint32_t /*metaDataValue1*/,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t /*metaDataValue1*/,
                                    uint32_t /*metaDataValue2*/,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/) override
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    /** @brief method to set the dbus Raw value Property with
     * the obtained progress code from the host.
     *
     *  @param[in] progressCodeBuffer - the progress Code SRC Buffer
     */
    virtual int setRawBootProperty(
        const std::tuple<uint64_t, std::vector<uint8_t>>& progressCodeBuffer);

    /** @brief Method to do necessary operation according different
     *         file type and being call when data transfer completed.
     *
     *  @param[in] IsWriteToMemOp - type of operation to decide what operation
     *                              needs to be done after data transfer.
     */
    virtual void postDataTransferCallBack(bool /*IsWriteToMemOp*/,
                                          uint32_t /*length*/) override
    {}

    /** @brief ProgressCodeHandler destructor
     */

    ~ProgressCodeHandler() {}
};

} // namespace responder
} // namespace pldm
