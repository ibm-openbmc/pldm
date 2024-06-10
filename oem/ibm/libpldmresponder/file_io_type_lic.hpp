#pragma once

#include "file_io.hpp"

namespace pldm
{
namespace responder
{

using LicType = uint16_t;

/** @class LicenseHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  to read license
 */
class LicenseHandler : public FileHandler
{
  public:
    /** @brief Handler constructor
     */
    LicenseHandler(uint32_t fileHandle, uint16_t fileType) :
        FileHandler(fileHandle), licType(fileType)
    {}

    virtual void writeFromMemory(uint32_t offset, uint32_t length,
                                 uint64_t address,
                                 oem_platform::Handler* /*oemPlatformHandler*/,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& event);

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

    virtual int write(const char* buffer, uint32_t /*offset*/, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int fileAck(uint8_t /*fileStatus*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t metaDataValue1,
                                    uint32_t metaDataValue2,
                                    uint32_t metaDataValue3,
                                    uint32_t metaDataValue4);
    virtual int newFileAvailableWithMetaData(uint64_t /*length*/,
                                             uint32_t /*metaDataValue1*/,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual void postDataTransferCallBack(bool IsWriteToMemOp, uint32_t length);

    int updateBinFileAndLicObjs(const fs::path& newLicFilePath);

    /** @brief LicenseHandler destructor
     */
    ~LicenseHandler() {}

  private:
    uint16_t licType;   //!< type of the license
    uint64_t licLength; //!< length of the full license data

    /** @brief PLDM CoD License install status
     */
    enum pldm_license_install_status
    {
        PLDM_LIC_ACTIVATED = 0x00,
        PLDM_LIC_INVALID_LICENSE = 0x01,
        PLDM_LIC_INCORRECT_SYSTEM = 0x02,
        PLDM_LIC_INCORRECT_SEQUENCE = 0x03,
        PLDM_LIC_PENDING = 0x04,
        PLDM_LIC_ACTIVATION_FAILED = 0x05,
        PLDM_LIC_INVALID_HOSTSTATE = 0x06,
    };
};
} // namespace responder
} // namespace pldm
