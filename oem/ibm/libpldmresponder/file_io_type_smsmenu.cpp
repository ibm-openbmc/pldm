#include "file_io_type_smsmenu.hpp"

#include "pfw-sms-utils/pfw_sms_menu.hpp"

#include <libpldm/base.h>
#include <libpldm/oem/ibm/file_io.h>
#include <stdint.h>

#include <iostream>

namespace pldm
{
using namespace pldm::responder::utils;
using namespace ibm_pfw_sms;

namespace responder
{

uint32_t SmsMenuHandler::readVecContent(std::vector<char>& inputVec,
                                        const uint32_t startIdx,
                                        const uint32_t endIdx)
{
    uint32_t size = 0;
    constexpr auto bitPos = 8;
    std::vector<char> userPassLenArr(inputVec.begin() + startIdx,
                                     inputVec.begin() + endIdx);
    for (uint32_t idx = 0; idx < bitPos; idx++)
    {
        size |= (uint32_t)userPassLenArr[idx] << bitPos * idx;
    }
    return size;
}

void SmsMenuHandler::postWriteAction(
    const uint16_t fileType, const uint32_t fileHandle,
    const struct fileack_status_metadata& metaDataObj)
{
    sdbusplus::message::object_path path;
    auto hostEid = pldm::utils::readHostEID();
    auto hostSockFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    dbusToFileHandlers
        .emplace_back(
            std::make_unique<pldm::requester::oem_ibm::DbusToFileHandler>(
                hostSockFd, hostEid, instanceIdDb, path, handler))
        ->sendFileAckWithMetaDataToHost(
            fileType, fileHandle, metaDataObj.fileStatus,
            metaDataObj.fileMetaData1, metaDataObj.fileMetaData2,
            metaDataObj.fileMetaData3, metaDataObj.fileMetaData4);
}

int SmsMenuHandler::write(const char* buffer, uint32_t /*offset*/,
                          uint32_t& length,
                          oem_platform::Handler* /*oemPlatformHandler*/,
                          struct fileack_status_metadata& metaDataObj)
{
    if (!buffer)
    {
        error("SMS Menu buffer is empty");
        return PLDM_ERROR;
    }

    std::vector<char> smsBuf(buffer, buffer + length);

    uint32_t userNewPassLen = 0;
    std::string newPassStr;

    // Format of file type USER_PASSWORD_AUTHENTICATION
    // <User Name Length><User Name><Password Length><Password>

    // Format of file type USER_PASSWORD_CHANGE
    // <User Name Length><User Name><Old Password Length><Old Password>
    // <New Password Length><New Password>

    // Read the size of User Name which is the content of first four bytes
    auto userNameLen = readVecContent(smsBuf, 0, sizeof(uint32_t));

    // Read the size of Password/Old Password which is the content of four bytes
    // after User Name
    auto userPassLen =
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
    auto result = vecSplit(smsBuf, sizeof(uint32_t),
                           sizeof(uint32_t) + userNameLen);
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

        metaDataObj.fileMetaData1 = (authenticated) ? PLDM_AUTHENTICATED
                                                    : PLDM_NOT_AUTHENTICATED;

        if (passwordChangeRequired)
        {
            metaDataObj.fileMetaData1 = PLDM_AUTHENTICATED_BUT_PASSWORD_EXPIRED;
        }
    }
    else if (smsMenuType == PLDM_FILE_TYPE_USER_PASSWORD_CHANGE)
    {
        changePassRc = changePassword(unameStr, passStr, newPassStr,
                                      operationAllowed);
        if (changePassRc == PASSWORD_CHANGE_SUCCESSFUL)
        {
            metaDataObj.fileMetaData1 = PLDM_PASSWORD_CHANGED;
        }
        else if (changePassRc == NOT_AUTHENTICATED)
        {
            metaDataObj.fileMetaData1 = PLDM_PASSWORD_NOT_CHANGED;
        }
        else
        {
            metaDataObj.fileMetaData1 = PLDM_PASSWORD_INVALID;
        }

        metaDataObj.fileMetaData3 = (int)changePassRc;
    }
    else
    {
        error("SMS Menu type is wrong");
        return PLDM_ERROR;
    }

    if (operationAllowed)
    {
        metaDataObj.fileMetaData2 = PLDM_ADMIN_OP_ALLOWED;
    }

    info("SMS PAM authenticated:{USR_AUTH} passwordChangeRequired:{USR_PASS_CR}"
         " operationAllowed:{OPA}",
         "USR_AUTH", authenticated, "USR_PASS_CR", passwordChangeRequired,
         "OPA", operationAllowed);

    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
