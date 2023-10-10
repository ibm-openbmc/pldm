#include "common/utils.hpp"
#include "file_io.hpp"
#include "file_io_type_cert.hpp"
#include "file_io_type_chap.hpp"
#include "file_io_type_dump.hpp"
#include "file_io_type_lic.hpp"
#include "file_io_type_lid.hpp"
#include "file_io_type_pcie.hpp"
#include "file_io_type_pel.hpp"
#include "file_io_type_progress_src.hpp"
#include "file_io_type_vpd.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <libpldm/base.h>
#include <libpldm/oem/ibm/file_io.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdeventplus;
using namespace sdeventplus::source;

void FileHandler::dmaResponseToRemoteTerminus(
    const SharedAIORespData& sharedAIORespDataobj,
    const pldm_completion_codes rStatus, uint32_t length)
{
    Response response(sizeof(pldm_msg_hdr) + sharedAIORespDataobj.command, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    encode_rw_file_by_type_memory_resp(sharedAIORespDataobj.instance_id,
                                       sharedAIORespDataobj.command, rStatus,
                                       length, responsePtr);
    if (nullptr != sharedAIORespDataobj.respInterface)
    {
        sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
    }
}

void FileHandler::dmaResponseToRemoteTerminus(
    const SharedAIORespData& sharedAIORespDataobj,
    const pldm_fileio_completion_codes rStatus, uint32_t length)
{
    Response response(sizeof(pldm_msg_hdr) + sharedAIORespDataobj.command, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    encode_rw_file_by_type_memory_resp(sharedAIORespDataobj.instance_id,
                                       sharedAIORespDataobj.command, rStatus,
                                       length, responsePtr);
    if (nullptr != sharedAIORespDataobj.respInterface)
    {
        sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
    }
}

void FileHandler::deleteAIOobjects(
    const std::shared_ptr<dma::DMA>& xdmaInterface,
    const SharedAIORespData& sharedAIORespDataobj)
{
    if (nullptr != xdmaInterface)
    {
        xdmaInterface->deleteIOInstance();
        (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
    }

    if (nullptr != sharedAIORespDataobj.functionPtr)
    {
        (static_cast<std::shared_ptr<FileHandler>>(
             sharedAIORespDataobj.functionPtr))
            .reset();
    }
}

void FileHandler::transferFileData(int fd, bool upstream, uint32_t offset,
                                   uint32_t& length, uint64_t address,
                                   SharedAIORespData& sharedAIORespDataobj,
                                   sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    if (nullptr == xdmaInterface)
    {
        error("Failed to initializing DMA interface during for fileType:{TYPE}",
              "TYPE", sharedAIORespDataobj.fileType);
        dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
        deleteAIOobjects(nullptr, sharedAIORespDataobj);
        close(fd);
        return;
    }
    xdmaInterface->setDMASourceFd(fd);
    uint32_t origLength = length;
    uint8_t command = sharedAIORespDataobj.command;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    dma::FileMetaData data{length, offset, address};
    xdmaInterface->setFileMetaData(data);
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->getTransferStatus())
        {
            error(
                "EventLoop Timeout..!! Terminating FileHandler data tranfer operation for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
        }
        return;
    };

    auto callback = [=, this](IO&, int, uint32_t revents) {
        if (!(revents & (EPOLLIN | EPOLLOUT)))
        {
            return;
        }
        auto wInterface = wxInterface.lock();
        dma::FileMetaData& data = wInterface->getFileMetaData();
        int rc = 0;
        while (data.length > dma::maxSize)
        {
            rc = wInterface->transferDataHost(fd, data.offset, dma::maxSize,
                                              data.address, upstream);
            data.length -= dma::maxSize;
            data.offset += dma::maxSize;
            data.address += dma::maxSize;
            if (rc < 0)
            {
                error(
                    "Failed to transfer muliple chunks of data to host fileType:{TYPE}",
                    "TYPE", sharedAIORespDataobj.fileType);
                dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR,
                                            0);
                deleteAIOobjects(wInterface, sharedAIORespDataobj);
                return;
            }
        }
        rc = wInterface->transferDataHost(fd, data.offset, data.length,
                                          data.address, upstream);
        if (rc < 0)
        {
            error(
                "transferFileData : Failed to transfer single chunks of data to host for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            deleteAIOobjects(wInterface, sharedAIORespDataobj);
            return;
        }
        if (static_cast<int>(data.length) == rc)
        {
            wInterface->setTransferStatus(true);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_SUCCESS,
                                        origLength);
            if (sharedAIORespDataobj.functionPtr != nullptr)
            {
                sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY,
                    data.length);
            }
            deleteAIOobjects(wInterface, sharedAIORespDataobj);
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getNewXdmaFd();
        if (xdmaFd < 0)
        {
            error("Failed to get the XDMA file descriptor for fileType:{TYPE}",
                  "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
            return;
        }
        if (xdmaInterface->initTimer(event, std::move(timerCb)) == false)
        {
            error("Failed to start the event timer for fileType:{TYPE}", "TYPE",
                  sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
            return;
        }
        xdmaInterface->insertIOInstance(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback)));
    }
    catch (const std::runtime_error& e)
    {
        error("Failed to start the event loop for fileType:{TYPE} : {ERROR}",
              "TYPE", sharedAIORespDataobj.fileType, "ERROR", e);
        dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
        deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
    }
}

void FileHandler::transferFileDataToSocket(
    int32_t fd, uint32_t& length, uint64_t address,
    SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    uint8_t command = sharedAIORespDataobj.command;
    if (nullptr == xdmaInterface)
    {
        error(
            "XDMA interface initialization failed while transfering data via socket for fileType:{TYPE}",
            "TYPE", sharedAIORespDataobj.fileType);
        dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
        if (sharedAIORespDataobj.functionPtr != nullptr)
        {
            sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
        }
        deleteAIOobjects(nullptr, sharedAIORespDataobj);
        return;
    }
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    dma::FileMetaData data{length, 0, address};
    xdmaInterface->setFileMetaData(data);
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    std::weak_ptr<FileHandler> wxfunctionPtr = sharedAIORespDataobj.functionPtr;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->getTransferStatus())
        {
            error(
                "EventLoop Timeout...Terminating tranfer operation while transfering data via socket for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            if (sharedAIORespDataobj.functionPtr != nullptr)
            {
                sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
            }
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
        }
        return;
    };
    auto callback = [=, this](IO&, int, uint32_t revents) {
        if (!(revents & (EPOLLIN | EPOLLOUT)))
        {
            return;
        }
        auto wInterface = wxInterface.lock();
        dma::FileMetaData& data = wInterface->getFileMetaData();
        int rc = 0;
        while (data.length > dma::maxSize)
        {
            rc = wInterface->transferHostDataToSocket(fd, dma::maxSize,
                                                      data.address);
            data.length -= dma::maxSize;
            data.address += dma::maxSize;
            if (rc < 0)
            {
                error(
                    "Failed to transfer muliple chunks of data to host while transfering data via socket for fileType:{TYPE}",
                    "TYPE", sharedAIORespDataobj.fileType);
                dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR,
                                            0);
                if (sharedAIORespDataobj.functionPtr != nullptr)
                {
                    sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                        command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
                }
                deleteAIOobjects(wInterface, sharedAIORespDataobj);
                return;
            }
        }
        rc = wInterface->transferHostDataToSocket(fd, data.length,
                                                  data.address);
        if (rc < 0)
        {
            error(
                "Failed to transfer single chunks of data to host while transfering data via socket for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            if (sharedAIORespDataobj.functionPtr != nullptr)
            {
                sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
            }
            deleteAIOobjects(wInterface, sharedAIORespDataobj);
            return;
        }
        if (static_cast<int>(data.length) == rc)
        {
            wInterface->setTransferStatus(true);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_SUCCESS,
                                        origLength);
            deleteAIOobjects(wInterface, sharedAIORespDataobj);
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getNewXdmaFd();
        if (xdmaFd < 0)
        {
            error(
                "Failed to open shared memory location while transfering data via socket for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            if (sharedAIORespDataobj.functionPtr != nullptr)
            {
                sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
            }
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
            return;
        }
        if (xdmaInterface->initTimer(event, std::move(timerCb)) == false)
        {
            error(
                "Failed to start the event timer while transfering data via socket for fileType:{TYPE}",
                "TYPE", sharedAIORespDataobj.fileType);
            dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
            if (sharedAIORespDataobj.functionPtr != nullptr)
            {
                sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
            }
            deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
            return;
        }
        xdmaInterface->insertIOInstance(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback)));
    }
    catch (const std::runtime_error& e)
    {
        error(
            "Failed to start the event loop while transfering data via socket for fileType:{TYPE} : {ERROR}",
            "TYPE", sharedAIORespDataobj.fileType, "ERROR", e);
        dmaResponseToRemoteTerminus(sharedAIORespDataobj, PLDM_ERROR, 0);
        if (sharedAIORespDataobj.functionPtr != nullptr)
        {
            sharedAIORespDataobj.functionPtr->postDataTransferCallBack(
                command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY, 0);
        }
        deleteAIOobjects(xdmaInterface, sharedAIORespDataobj);
    }
    return;
}

void FileHandler::transferFileData(const fs::path& path, bool upstream,
                                   uint32_t offset, uint32_t& length,
                                   uint64_t address,
                                   SharedAIORespData& sharedAIORespDataobj,
                                   sdeventplus::Event& event)
{
    bool fileExists = false;
    if (upstream)
    {
        fileExists = fs::exists(path);
        if (!fileExists)
        {
            error("File '{PATH}' does not exist.", "PATH", path);
            FileHandler::dmaResponseToRemoteTerminus(
                sharedAIORespDataobj, PLDM_INVALID_FILE_HANDLE, length);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        }

        size_t fileSize = fs::file_size(path);
        if (offset >= fileSize)
        {
            error(
                "Offset '{OFFSET}' exceeds file size '{SIZE}' for file handle {FILE_HANDLE}",
                "OFFSET", offset, "SIZE", fileSize, "FILE_HANDLE", fileHandle);
            FileHandler::dmaResponseToRemoteTerminus(
                sharedAIORespDataobj, PLDM_DATA_OUT_OF_RANGE, length);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        }
        if (offset + length > fileSize)
        {
            length = fileSize - offset;
        }
    }

    int flags{};
    if (upstream)
    {
        flags = O_RDONLY;
    }
    else if (fileExists)
    {
        flags = O_RDWR;
    }
    else
    {
        flags = O_WRONLY;
    }
    int file = open(path.string().c_str(), flags);
    if (file == -1)
    {
        error("File '{PATH}' does not exist.", "PATH", path);
        return;
        FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                 PLDM_ERROR, 0);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
    }
    transferFileData(file, upstream, offset, length, address,
                     sharedAIORespDataobj, event);
}

std::unique_ptr<FileHandler>
    getHandlerByType(uint16_t fileType, uint32_t fileHandle)
{
    switch (fileType)
    {
        case PLDM_FILE_TYPE_PEL:
        {
            return std::make_unique<PelHandler>(fileHandle);
        }
        case PLDM_FILE_TYPE_LID_PERM:
        {
            return std::make_unique<LidHandler>(fileHandle, true);
        }
        case PLDM_FILE_TYPE_LID_TEMP:
        {
            return std::make_unique<LidHandler>(fileHandle, false);
        }
        case PLDM_FILE_TYPE_LID_MARKER:
        {
            return std::make_unique<LidHandler>(fileHandle, false,
                                                PLDM_FILE_TYPE_LID_MARKER);
        }
        case PLDM_FILE_TYPE_DUMP:
        case PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS:
        case PLDM_FILE_TYPE_RESOURCE_DUMP:
        {
            return std::make_unique<DumpHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_CERT_SIGNING_REQUEST:
        case PLDM_FILE_TYPE_SIGNED_CERT:
        case PLDM_FILE_TYPE_ROOT_CERT:
        {
            return std::make_unique<CertHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PROGRESS_SRC:
        {
            return std::make_unique<ProgressCodeHandler>(fileHandle);
        }
        case PLDM_FILE_TYPE_COD_LICENSE_KEY:
        case PLDM_FILE_TYPE_COD_LICENSED_RESOURCES:
        {
            return std::make_unique<LicenseHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_LID_RUNNING:
        {
            return std::make_unique<LidHandler>(fileHandle, false,
                                                PLDM_FILE_TYPE_LID_RUNNING);
        }
        case PLDM_FILE_TYPE_PSPD_VPD_PDD_KEYWORD:
        {
            return std::make_unique<keywordHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PCIE_TOPOLOGY:
        case PLDM_FILE_TYPE_CABLE_INFO:
        {
            return std::make_unique<PCIeInfoHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_CHAP_DATA:
        {
            return std::make_unique<ChapHandler>(fileHandle, fileType);
        }
        default:
        {
            throw InternalFailure();
            break;
        }
    }
    return nullptr;
}

std::shared_ptr<FileHandler> getSharedHandlerByType(uint16_t fileType,
                                                    uint32_t fileHandle)
{
    switch (fileType)
    {
        case PLDM_FILE_TYPE_PEL:
        {
            return std::make_shared<PelHandler>(fileHandle);
        }
        case PLDM_FILE_TYPE_LID_PERM:
        {
            return std::make_shared<LidHandler>(fileHandle, true);
        }
        case PLDM_FILE_TYPE_LID_TEMP:
        {
            return std::make_shared<LidHandler>(fileHandle, false);
        }
        case PLDM_FILE_TYPE_LID_MARKER:
        {
            return std::make_shared<LidHandler>(fileHandle, false,
                                                PLDM_FILE_TYPE_LID_MARKER);
        }
        case PLDM_FILE_TYPE_LID_RUNNING:
        {
            return std::make_shared<LidHandler>(fileHandle, false,
                                                PLDM_FILE_TYPE_LID_RUNNING);
        }
        case PLDM_FILE_TYPE_DUMP:
        case PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS:
        case PLDM_FILE_TYPE_RESOURCE_DUMP:
        case PLDM_FILE_TYPE_BMC_DUMP:
        case PLDM_FILE_TYPE_SBE_DUMP:
            // case PLDM_FILE_TYPE_HOSTBOOT_DUMP:
            // case PLDM_FILE_TYPE_HARDWARE_DUMP:
            {
                return std::make_shared<DumpHandler>(fileHandle, fileType);
            }
        case PLDM_FILE_TYPE_CERT_SIGNING_REQUEST:
        case PLDM_FILE_TYPE_SIGNED_CERT:
        case PLDM_FILE_TYPE_ROOT_CERT:
        {
            return std::make_shared<CertHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PROGRESS_SRC:
        {
            return std::make_shared<ProgressCodeHandler>(fileHandle);
        }
        case PLDM_FILE_TYPE_COD_LICENSE_KEY:
        case PLDM_FILE_TYPE_COD_LICENSED_RESOURCES:
        {
            return std::make_shared<LicenseHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PCIE_TOPOLOGY:
        case PLDM_FILE_TYPE_CABLE_INFO:
        {
            return std::make_shared<PCIeInfoHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PSPD_VPD_PDD_KEYWORD:
        {
            return std::make_shared<keywordHandler>(fileHandle, fileType);
        }
        default:
        {
            throw InternalFailure();
            break;
        }
    }
    return nullptr;
}

int FileHandler::readFile(const std::string& filePath, uint32_t offset,
                          uint32_t& length, Response& response)
{
    if (!fs::exists(filePath))
    {
        error("File '{PATH}' and handle {FILE_HANDLE} does not exist", "PATH",
              filePath, "FILE_HANDLE", fileHandle);
        return PLDM_INVALID_FILE_HANDLE;
    }

    size_t fileSize = fs::file_size(filePath);
    if (offset >= fileSize)
    {
        error(
            "Offset '{OFFSET}' exceeds file size '{SIZE}' and file handle '{FILE_HANDLE}'",
            "OFFSET", offset, "SIZE", fileSize, "FILE_HANDLE", fileHandle);
        return PLDM_DATA_OUT_OF_RANGE;
    }

    if (offset + length > fileSize)
    {
        length = fileSize - offset;
    }

    size_t currSize = response.size();
    response.resize(currSize + length);
    auto filePos = reinterpret_cast<char*>(response.data());
    filePos += currSize;
    std::ifstream stream(filePath, std::ios::in | std::ios::binary);
    if (stream)
    {
        stream.seekg(offset);
        stream.read(filePos, length);
        return PLDM_SUCCESS;
    }
    error(
        "Unable to read file '{PATH}' at offset '{OFFSET}' for length '{LENGTH}'",
        "PATH", filePath, "OFFSET", offset, "LENGTH", length);
    return PLDM_ERROR;
}

} // namespace responder
} // namespace pldm
