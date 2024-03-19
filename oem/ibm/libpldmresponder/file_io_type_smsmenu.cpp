#include "file_io_type_smsmenu.hpp"

#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include "pfw-sms-utils/pfw_sms_menu.hpp"

#include <stdint.h>

#include <iostream>

namespace pldm
{
using namespace pldm::responder::utils;
using namespace ibm_pfw_sms;

namespace responder
{

int SmsMenuHandler::write(const char* buffer, uint32_t /*offset*/,
                          uint32_t& length,
                          oem_platform::Handler* /*oemPlatformHandler*/,
                          struct fileack_status_metadata& metaDataObj)
{
    if (buffer == nullptr)
    {
        error("SMS Menu buffer is empty");
        return PLDM_ERROR;
    }

    std::vector<char> smsBuf(buffer, buffer + length);

    uint32_t userNameLen = 0;
    uint32_t userPassLen = 0;
    uint32_t userNewPassLen = 0;
    std::vector<char> result;
    std::string newPassStr;

    // Format of file type USER_PASSWORD_AUTHENTICATION
    // <User Name Length><User Name><Password Length><Password>

    // Format of file type USER_PASSWORD_CHANGE
    // <User Name Length><User Name><Old Password Length><Old Password>
    // <New Password Length><New Password>

    // Read the size of User Name which is the content of first four bytes
    userNameLen = readVecContent(smsBuf, 0, sizeof(uint32_t));

    // Read the size of Password/Old Password which is the content of four bytes
    // after User Name
    userPassLen =
        readVecContent(smsBuf, sizeof(uint32_t) + userNameLen,
                       sizeof(uint32_t) + userNameLen + sizeof(uint32_t));
    if (smsMenuType == PLDM_FILE_TYPE_USER_PASSWORD_CHANGE)
    {
        // Read the size of New Password which is the content of four bytes
        // after Old Password
        userNewPassLen = readVecContent(
            smsBuf,
            sizeof(uint32_t) + userNameLen + sizeof(uint32_t) + userPassLen,
            sizeof(uint32_t) + userNameLen + sizeof(uint32_t) + userPassLen +
                sizeof(uint32_t));
    }

    // Split the vector to retrieve the User Name
    result = vecSplit(smsBuf, sizeof(uint32_t), sizeof(uint32_t) + userNameLen);
    std::string unameStr(result.begin(), result.end());

    // Split the vector to retrieve the Password/Old Password
    result = vecSplit(smsBuf, sizeof(uint32_t) + userNameLen + sizeof(uint32_t),
                      sizeof(uint32_t) + userNameLen + sizeof(uint32_t) +
                          userPassLen);
    std::string passStr(result.begin(), result.end());

    if (smsMenuType == PLDM_FILE_TYPE_USER_PASSWORD_CHANGE)
    {
        // Split the vector to retrieve the New Password
        result = vecSplit(smsBuf,
                          sizeof(uint32_t) + userNameLen + sizeof(uint32_t) +
                              userPassLen + sizeof(uint32_t),
                          sizeof(uint32_t) + userNameLen + sizeof(uint32_t) +
                              userPassLen + sizeof(uint32_t) + userNewPassLen);
        std::string tempNewPassStr(result.begin(), result.end());
        newPassStr = tempNewPassStr;
    }

    metaDataObj.fileStatus = 0x00;
    metaDataObj.fileMetaData1 = 0xFFFFFFFF;
    metaDataObj.fileMetaData2 = 0xFFFFFFFF;
    metaDataObj.fileMetaData3 = 0xFFFFFFFF;
    metaDataObj.fileMetaData4 = 0xFFFFFFFF;

    bool authenticated = false;
    bool passwordChangeRequired = false;
    bool operationAllowed = false;
    changePasswordReasonCode changePassRc{};

    if (smsMenuType == PLDM_FILE_TYPE_USER_PASSWORD_AUTHENTICATION)
    {
        authenticate(unameStr, passStr, authenticated, passwordChangeRequired,
                     operationAllowed);
        if (authenticated == true)
        {
            metaDataObj.fileMetaData1 = PasswordAuthResult::AUTHENTICATED;
        }
        else
        {
            metaDataObj.fileMetaData1 = PasswordAuthResult::NOT_AUTHENTICATED;
        }

        if (passwordChangeRequired == true)
        {
            metaDataObj.fileMetaData1 =
                PasswordAuthResult::AUTHENTICATED_BUT_EXPIRED;
        }

        if (operationAllowed == true)
        {
            metaDataObj.fileMetaData2 = AdminOperationStatus::ALLOWED;
        }
    }
    else if (smsMenuType == PLDM_FILE_TYPE_USER_PASSWORD_CHANGE)
    {
        changePassRc = changePassword(unameStr, passStr, newPassStr,
                                      operationAllowed);
        if (changePassRc == PASSWORD_CHANGE_SUCCESSFUL)
        {
            metaDataObj.fileMetaData1 = PasswordChangeResult::CHANGED;
        }
        else if (changePassRc == ibm_pfw_sms::NOT_AUTHENTICATED)
        {
            metaDataObj.fileMetaData1 = PasswordChangeResult::NOT_CHANGED;
        }
        else
        {
            metaDataObj.fileMetaData1 = PasswordChangeResult::INVALID_PASSWORD;
        }

        if (operationAllowed == true)
        {
            metaDataObj.fileMetaData2 = AdminOperationStatus::ALLOWED;
        }

        metaDataObj.fileMetaData3 = (int)changePassRc;
    }
    info("SMS PAM authenticated:{USR_AUTH} passwordChangeRequired:{USR_PASS_CR}"
         " operationAllowed:{OPA}",
         "USR_AUTH", authenticated, "USR_PASS_CR", passwordChangeRequired,
         "OPA", operationAllowed);

    return PLDM_SUCCESS;
}

int SmsMenuHandler::newFileAvailable(uint64_t length)
{
    smsDataLength = length;
    return PLDM_SUCCESS;
}

int SmsMenuHandler::fileAckWithMetaData(uint8_t /*fileStatus*/,
                                        uint32_t /*metaDataValue1*/,
                                        uint32_t /*metaDataValue2*/,
                                        uint32_t /*metaDataValue3*/,
                                        uint32_t /*metaDataValue4*/)
{
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
