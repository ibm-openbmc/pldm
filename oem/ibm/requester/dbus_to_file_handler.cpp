#include "dbus_to_file_handler.hpp"

#include "libpldm/file_io.h"
#include "libpldm/pldm.h"

#include "common/utils.hpp"
#include "pfw-sms-utils/pfw_sms_menu.hpp"

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace requester
{
namespace oem_ibm
{
using namespace pldm::utils;
using namespace sdbusplus::bus::match::rules;
using namespace ibm_pfw_sms;

static constexpr auto resDumpObjPath =
    "/xyz/openbmc_project/dump/resource/entry";
static constexpr auto resDumpEntry = "com.ibm.Dump.Entry.Resource";
static constexpr auto resDumpProgressIntf =
    "xyz.openbmc_project.Common.Progress";
static constexpr auto resDumpStatus =
    "xyz.openbmc_project.Common.Progress.OperationStatus.Failed";
static constexpr auto certFilePath = "/var/lib/ibm/bmcweb/";
constexpr auto certObjPath = "/xyz/openbmc_project/certs/ca/entry/";
constexpr auto certEntryIntf = "xyz.openbmc_project.Certs.Entry";
DbusToFileHandler::DbusToFileHandler(
    int mctp_fd, uint8_t mctp_eid, dbus_api::Requester* requester,
    sdbusplus::message::object_path resDumpCurrentObjPath,
    pldm::requester::Handler<pldm::requester::Request>* handler) :
    mctp_fd(mctp_fd),
    mctp_eid(mctp_eid), requester(requester),
    resDumpCurrentObjPath(resDumpCurrentObjPath), handler(handler)
{}

void DbusToFileHandler::sendNewFileAvailableCmd(uint64_t fileSize)
{
    if (requester == NULL)
    {
        error(
            "Failed to send resource dump parameters as requester is not set");
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.sendNewFileAvailableCmd.SendDumpParametersFail",
            pldm::PelSeverity::ERROR);
        return;
    }
    auto instanceId = requester->getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_NEW_FILE_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    uint32_t fileHandle =
        atoi(fs::path((std::string)resDumpCurrentObjPath).filename().c_str());

    auto rc = encode_new_file_req(instanceId,
                                  PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS,
                                  fileHandle, fileSize, request);
    if (rc != PLDM_SUCCESS)
    {
        requester->markFree(mctp_eid, instanceId);
        error("Failed to encode_new_file_req, rc = {RC}", "RC", rc);
        return;
    }

    auto newFileAvailableRespHandler = [this](mctp_eid_t /*eid*/,
                                              const pldm_msg* response,
                                              size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error("Failed to receive response for NewFileAvailable command");
            return;
        }
        uint8_t completionCode{};
        auto rc = decode_new_file_resp(response, respMsgLen, &completionCode);
        if (rc || completionCode)
        {
            error(
                "Failed to decode_new_file_resp or Host returned error for new_file_available rc={RC}, cc = {CC}",
                "RC", rc, "CC", static_cast<unsigned>(completionCode));
            reportResourceDumpFailure("decodeNewFileResp");
        }
    };
    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_OEM, PLDM_NEW_FILE_AVAILABLE,
        std::move(requestMsg), std::move(newFileAvailableRespHandler));
    if (rc)
    {
        error("Failed to send NewFileAvailable Request to Host");
        reportResourceDumpFailure("newFileAvailableRequest");
    }
}

void DbusToFileHandler::reportResourceDumpFailure(std::string str)
{
    std::string s = "xyz.openbmc_project.PLDM.Error.ReportResourceDumpFail." +
                    str;

    pldm::utils::reportError(s.c_str(), pldm::PelSeverity::WARNING);

    PropertyValue value{resDumpStatus};
    DBusMapping dbusMapping{resDumpCurrentObjPath, resDumpProgressIntf,
                            "Status", "string"};
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error("failed to set resource dump operation status, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
    }
}

void DbusToFileHandler::processNewResourceDump(
    const std::string& vspString, const std::string& resDumpReqPass)
{
    try
    {
        std::string objPath = resDumpCurrentObjPath;
        auto propVal = pldm::utils::DBusHandler().getDbusPropertyVariant(
            objPath.c_str(), "Status", resDumpProgressIntf);
        const auto& curResDumpStatus = std::get<std::string>(propVal);

        if (curResDumpStatus !=
            "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress")
        {
            return;
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error(
            "Error {ERR_EXCEP} found in getting current resource dump status while initiating a new resource dump with objPath={DUMP_OBJ_PATH} and intf={DUMP_PROG_INTF}",
            "ERR_EXCEP", e.what(), "DUMP_OBJ_PATH",
            resDumpCurrentObjPath.str.c_str(), "DUMP_PROG_INTF",
            resDumpProgressIntf);

        return;
    }

    namespace fs = std::filesystem;
    const fs::path resDumpDirPath = "/var/lib/pldm/resourcedump";

    if (!fs::exists(resDumpDirPath))
    {
        fs::create_directories(resDumpDirPath);
    }

    uint32_t fileHandle =
        atoi(fs::path((std::string)resDumpCurrentObjPath).filename().c_str());
    fs::path resDumpFilePath = resDumpDirPath / std::to_string(fileHandle);

    std::ofstream fileHandleFd;
    fileHandleFd.open(resDumpFilePath, std::ios::out | std::ofstream::binary);

    if (!fileHandleFd)
    {
        error("resource dump file open error:{RES_DUMP_PATH}", "RES_DUMP_PATH",
              resDumpFilePath);
        PropertyValue value{resDumpStatus};
        DBusMapping dbusMapping{resDumpCurrentObjPath, resDumpProgressIntf,
                                "Status", "string"};
        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "failed to set resource dump operation status, ERROR={ERR_EXCEP}",
                "ERR_EXCEP", e.what());
        }
        return;
    }

    // Fill up the file with resource dump parameters and respective sizes
    auto fileFunc = [&fileHandleFd](auto& paramBuf) {
        uint32_t paramSize = paramBuf.size();
        fileHandleFd.write((char*)&paramSize, sizeof(paramSize));
        fileHandleFd << paramBuf;
    };
    fileFunc(vspString);
    fileFunc(resDumpReqPass);

    std::string str;
    if (!resDumpReqPass.empty())
    {
        str = getAcfFileContent();
    }

    fileFunc(str);

    fileHandleFd.close();
    size_t fileSize = fs::file_size(resDumpFilePath);

    sendNewFileAvailableCmd(fileSize);
}

std::string DbusToFileHandler::getAcfFileContent()
{
    std::string str;
    static constexpr auto acfDirPath = "/etc/acf/service.acf";
    if (fs::exists(acfDirPath))
    {
        std::ifstream file;
        file.open(acfDirPath);
        std::stringstream acfBuf;
        acfBuf << file.rdbuf();
        str = acfBuf.str();
        file.close();
    }
    return str;
}

void DbusToFileHandler::newCsrFileAvailable(const std::string& csr,
                                            const std::string fileHandle)
{
    namespace fs = std::filesystem;
    std::string dirPath = "/var/lib/ibm/bmcweb";
    const fs::path certDirPath = dirPath;

    if (!fs::exists(certDirPath))
    {
        fs::create_directories(certDirPath);
        fs::permissions(certDirPath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path certFilePath = certDirPath / ("CSR_" + fileHandle);
    std::ofstream certFile;

    certFile.open(certFilePath, std::ios::out | std::ofstream::binary);

    if (!certFile)
    {
        error("cert file open error: {CERT_FILE}", "CERT_FILE",
              certFilePath.c_str());
        return;
    }

    // Add csr to file
    certFile << csr << std::endl;

    certFile.close();
    uint32_t fileSize = fs::file_size(certFilePath);

    newFileAvailableSendToHost(fileSize, (uint32_t)stoi(fileHandle),
                               PLDM_FILE_TYPE_CERT_SIGNING_REQUEST);
}

void DbusToFileHandler::newChapDataFileAvailable(
    const std::string& chapNameStr, const std::string& chapPasswordStr)
{
    namespace fs = std::filesystem;
    const fs::path chapDataDirPath = "/var/lib/pldm/ChapData/";
    if (!fs::exists(chapDataDirPath))
    {
        fs::create_directories(chapDataDirPath);
        fs::permissions(chapDataDirPath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path chapFilePath = std::string(chapDataDirPath) +
                            std::string("chapsecret");
    uint32_t fileHandle = atoi(fs::path((std::string)chapFilePath).c_str());
    std::ofstream fileHandleFd;
    fileHandleFd.open(chapFilePath, std::ios::out | std::ofstream::binary);
    if (!fileHandleFd)
    {
        error("chap data file open error:{CHAP_PATH}", "CHAP_PATH",
              chapFilePath);
        return;
    }

    // Fill up the file with chap data parameters and respective sizes
    auto fileFunc = [&fileHandleFd](auto& paramBuf) {
        uint32_t paramSize = paramBuf.size();
        fileHandleFd.write((char*)&paramSize, sizeof(paramSize));
        fileHandleFd << paramBuf;
    };
    fileFunc(chapNameStr);
    fileFunc(chapPasswordStr);
    if (fileHandleFd.bad())
    {
        error("Error while writing to chap file: {CHAPFILE_PATH}",
              "CHAPFILE_PATH", chapFilePath);
    }
    fileHandleFd.close();
    size_t fileSize = fs::file_size(chapFilePath);

    newFileAvailableSendToHost(fileSize, fileHandle, PLDM_FILE_TYPE_CHAP_DATA);
}

void DbusToFileHandler::newLicFileAvailable(const std::string& licenseStr)
{
    namespace fs = std::filesystem;
    std::string dirPath = "/var/lib/ibm/cod";
    const fs::path licDirPath = dirPath;

    if (!fs::exists(licDirPath))
    {
        fs::create_directories(licDirPath);
        fs::permissions(licDirPath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path licFilePath = licDirPath / "licFile";
    std::ofstream licFile;

    licFile.open(licFilePath, std::ios::out | std::ofstream::binary);

    if (!licFile)
    {
        error("License file open error: {LIC_FILE}", "LIC_FILE",
              licFilePath.c_str());
        return;
    }

    // Add csr to file
    licFile << licenseStr << std::endl;

    licFile.close();
    uint32_t fileSize = fs::file_size(licFilePath);

    newFileAvailableSendToHost(fileSize, 1, PLDM_FILE_TYPE_COD_LICENSE_KEY);
}

void DbusToFileHandler::newFileAvailableSendToHost(const uint32_t fileSize,
                                                   const uint32_t fileHandle,
                                                   const uint16_t type)
{
    if (requester == NULL)
    {
        error("newFileAvailableSendToHost:Failed to send file to host.");
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.SendFileToHostFail",
            pldm::PelSeverity::ERROR);
        return;
    }
    auto instanceId = requester->getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_NEW_FILE_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());

    auto rc = encode_new_file_req(instanceId, type, fileHandle, fileSize,
                                  request);
    if (rc != PLDM_SUCCESS)
    {
        requester->markFree(mctp_eid, instanceId);
        error(
            "newFileAvailableSendToHost:Failed to encode_new_file_req, rc = {RC}",
            "RC", rc);
        return;
    }
    info(
        "newFileAvailableSendToHost:Sending Sign CSR request to Host for fileHandle: {FILE_HNDL}",
        "FILE_HNDL", fileHandle);
    auto newFileAvailableRespHandler =
        [fileHandle, type](mctp_eid_t /*eid*/, const pldm_msg* response,
                           size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error("Failed to receive response for NewFileAvailable command");
            if (type == PLDM_FILE_TYPE_CERT_SIGNING_REQUEST)
            {
                std::string filePath = certFilePath;
                filePath += "CSR_" + std::to_string(fileHandle);
                fs::remove(filePath);

                DBusMapping dbusMapping{certObjPath +
                                            std::to_string(fileHandle),
                                        certEntryIntf, "Status", "string"};
                PropertyValue value =
                    "xyz.openbmc_project.Certs.Entry.State.Pending";
                try
                {
                    pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                               value);
                }
                catch (const std::exception& e)
                {
                    error(
                        "newFileAvailableSendToHost:Failed to set status property of certicate entry, ERROR={ERR_EXCEP}",
                        "ERR_EXCEP", e.what());
                }

                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.SendFileToHostFail",
                    pldm::PelSeverity::INFORMATIONAL);
            }
            return;
        }
        uint8_t completionCode{};
        auto rc = decode_new_file_resp(response, respMsgLen, &completionCode);
        if (rc || completionCode)
        {
            error(
                "Failed to decode_new_file_resp for file, or Host returned error for new_file_available rc = {RC}, cc= {CC}",
                "RC", rc, "CC", static_cast<unsigned>(completionCode));
            if (rc)
            {
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.DecodeNewFileResponseFail",
                    pldm::PelSeverity::ERROR);
            }
        }
    };
    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_OEM, PLDM_NEW_FILE_AVAILABLE,
        std::move(requestMsg), std::move(newFileAvailableRespHandler));
    if (rc)
    {
        error(
            "newFileAvailableSendToHost:Failed to send NewFileAvailable Request to Host");
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.NewFileAvailableRequestFail",
            pldm::PelSeverity::ERROR);
    }
}

void DbusToFileHandler::sendFileAckWithMetaDataToHost(
    uint16_t fileType, uint32_t fileHandle, uint8_t fileStatus,
    uint32_t fileMetaData1, uint32_t fileMetaData2, uint32_t fileMetaData3,
    uint32_t fileMetaData4)
{
    info(
        "sending acknowledgement with metadata to host where metadata1:{FMD1} metadata2:{FMD2} metadata3:{FMD3} "
        "metadata4:{FMD4}",
        "FMD1", fileMetaData1, "FMD2", fileMetaData2, "FMD3", fileMetaData3,
        "FMD4", fileMetaData4);
    if (!requester)
    {
        error("Failed to send sms menu data validation response as "
              "requester is not set");
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.sendFileAckWithMetaDataToHost."
            "SendSmsMenuRespFail",
            pldm::PelSeverity::ERROR);
        return;
    }
    auto instanceId = requester->getInstanceId(mctp_eid);
    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_FILE_ACK_WITH_META_DATA_REQ_BYTES);
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());

    auto rc = encode_file_ack_with_meta_data_req(
        instanceId, fileType, fileHandle, fileStatus, fileMetaData1,
        fileMetaData2, fileMetaData3, fileMetaData4, request);
    if (rc != PLDM_SUCCESS)
    {
        requester->markFree(mctp_eid, instanceId);
        error(
            "Failed to encode_file_ack_with_meta_data_req, response code:{RC}",
            "RC", rc);
        return;
    }

    auto fileAckWithMetaDataRespHandler = [this](mctp_eid_t /*eid*/,
                                                 const pldm_msg* response,
                                                 size_t respMsgLen) {
        if (response == nullptr || !respMsgLen)
        {
            error("Failed to receive response for FileAckWithMetaData command");
            return;
        }
        uint8_t completionCode{};
        auto rc = decode_file_ack_with_meta_data_resp(response, respMsgLen,
                                                      &completionCode);
        if (rc || completionCode)
        {
            error(
                "Failed to decode_file_ack_with_meta_data_resp or Host returned"
                " error for file_ack_with_meta_data response code:{RC}, "
                "completion code:{CC}",
                "RC", rc, "CC", completionCode);
            reportResourceDumpFailure("decodeFileAckWithMetaDataResp");
        }
    };
    rc = handler->registerRequest(
        mctp_eid, instanceId, PLDM_OEM, PLDM_FILE_ACK_WITH_META_DATA,
        std::move(requestMsg), std::move(fileAckWithMetaDataRespHandler));
    if (rc)
    {
        error("Failed to send FileAckWithMetaData Request to Host");
        reportResourceDumpFailure("fileAckWithMeataDataRequest");
    }
}

} // namespace oem_ibm
} // namespace requester
} // namespace pldm
