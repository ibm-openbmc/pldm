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
    SmsMenuHandler(uint32_t fileHandle, uint16_t fileType) :
        FileHandler(fileHandle), smsMenuType(fileType)
    {}

    virtual int writeFromMemory(uint32_t /*offset*/, uint32_t /*length*/,
                                uint64_t /*address*/,
                                oem_platform::Handler* /*oemPlatformHandler*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    virtual int readIntoMemory(uint32_t /*offset*/, uint32_t& /*length*/,
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

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t /*metaDataValue1*/,
                                    uint32_t /*metaDataValue2*/,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/);

    virtual int newFileAvailableWithMetaData(uint64_t /*length*/,
                                             uint32_t /*metaDataValue1*/,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

    /** @brief SmsMenuHandler destructor
     */
    ~SmsMenuHandler() {}

  private:
    uint16_t smsMenuType;   //!< type of the SMS menu selection
    uint64_t smsDataLength; //!< length of the SMS data

    enum PasswordAuthResult
    {
        AUTHENTICATED = 0x00,     //!< Authenticated successfully
        AUTHENTICATED_BUT_EXPIRED =
            0x01,                 //!< Authenticated, but password expired
        NOT_AUTHENTICATED = 0x02, //!< Not Authenticated,
                                  //!< username or password invalid
    };

    enum PasswordChangeResult
    {
        CHANGED = 0x00,          //!< Password changed successfully
        NOT_CHANGED = 0x02,      //!< Not Changed,
                                 //!< username or password invalid
        INVALID_PASSWORD = 0x03, //!< New password is invalid, as it does not
                                 //!< adhere to password rules
    };

    enum AdminOperationStatus
    {
        NOT_ALLOWED = 0x00, //!< Not an admin role
        ALLOWED = 0x01,     //!< Admin role so, admin operations allowed
    };
};
} // namespace responder
} // namespace pldm
