#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{

/** @class SmsMenuHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  to support password prompt for the graphical SMS(System Management Services)
 *  menus provided by PFW(Platform Firmware).Supported operations are
 *  user password authentication and user password change.
 */
class SmsMenuHandler : public FileHandler
{
  public:
    /** @brief Handler constructor
     */
    SmsMenuHandler(
        uint32_t fileHandle, uint16_t fileType,
        dbus_api::Requester* dbusImplReqester,
        pldm::requester::Handler<pldm::requester::Request>* handler) :
        FileHandler(fileHandle),
        smsMenuType(fileType), dbusImplReqester(dbusImplReqester),
        handler(handler)
    {}

    virtual int writeFromMemory(uint32_t /*offset*/, uint32_t /*length*/,
                                uint64_t /*address*/,
                                oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int readIntoMemory(uint32_t /*offset*/, uint32_t /*length*/,
                               uint64_t /*address*/,
                               oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int read(uint32_t /*offset*/, uint32_t& /*length*/,
                     Response& /*response*/,
                     oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int write(const char* buffer, uint32_t /*offset*/, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/,
                      struct fileack_status_metadata& metaDataObj);

    virtual int fileAck(uint8_t /*fileStatus*/)
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

    virtual void
        postWriteAction(const uint16_t fileType, const uint32_t fileHandle,
                        const struct fileack_status_metadata& metaDataObj);

    /** @brief Reads the contents of the input vector and returns the value back
     *  @param[in] inputVec - input vector
     *  @param[in] startIdx - start index
     *  @param[in] endIdx - end index
     *  @return  the value contained in the input vector
     */
    uint32_t readVecContent(std::vector<char>& inputVec,
                            const uint32_t startIdx, const uint32_t endIdx);

    /** @brief SmsMenuHandler destructor
     */
    ~SmsMenuHandler() {}

  private:
    uint16_t smsMenuType; //!< type of the SMS menu selection

    // pldm::requester::Handler<pldm::requester::Request>* handler;
    std::vector<std::unique_ptr<pldm::requester::oem_ibm::DbusToFileHandler>>
        dbusToFileHandlers;
    /** @brief pldm dbus api requester */
    dbus_api::Requester* dbusImplReqester;
    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;
};
} // namespace responder
} // namespace pldm
