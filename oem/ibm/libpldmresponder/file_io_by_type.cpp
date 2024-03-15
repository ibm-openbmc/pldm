#include "file_io_by_type.hpp"

#include "libpldm/base.h"
#include "libpldm/file_io.h"

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

#include <stdint.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdeventplus;
using namespace sdeventplus::source;

void FileHandler::dmaResponseToHost(const ResponseHdr& responseHdr,
                                    const pldm_completion_codes rStatus,
                                    uint32_t length)
{
    Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                       responseHdr.command, rStatus, length,
                                       responsePtr);
    if (nullptr != responseHdr.respInterface)
    {
        responseHdr.respInterface->sendPLDMRespMsg(response, responseHdr.key);
    }
}

void FileHandler::dmaResponseToHost(const ResponseHdr& responseHdr,
                                    const pldm_fileio_completion_codes rStatus,
                                    uint32_t length)
{
    Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                       responseHdr.command, rStatus, length,
                                       responsePtr);
    if (nullptr != responseHdr.respInterface)
    {
        responseHdr.respInterface->sendPLDMRespMsg(response, responseHdr.key);
    }
}

void FileHandler::deleteAIOobjects(
    const std::shared_ptr<dma::DMA>& xdmaInterface,
    const ResponseHdr& responseHdr)
{
    if (nullptr != xdmaInterface)
    {
        xdmaInterface->deleteIOInstance();
        (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
    }

    if (nullptr != responseHdr.functionPtr)
    {
        (static_cast<std::shared_ptr<FileHandler>>(responseHdr.functionPtr))
            .reset();
    }
}

void FileHandler::transferFileData(int32_t fd, bool upstream, uint32_t offset,
                                   uint32_t& length, uint64_t address,
                                   ResponseHdr& responseHdr,
                                   sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    if (nullptr == xdmaInterface)
    {
        error("transferFileData : xdma interface initialization failed.\n");
        dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
        deleteAIOobjects(nullptr, responseHdr);
        close(fd);
        return;
    }
    xdmaInterface->setDMASourceFd(fd);
    uint32_t origLength = length;
    uint8_t command = responseHdr.command;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    dma::FileMetaData data;
    data.length = length;
    data.offset = offset;
    data.address = address;
    xdmaInterface->setFileMetaData(data);
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->getResponseReceived())
        {
            error(
                "transferFileData : EventLoop Timeout..!! Terminating FileHandler data tranfer operation.\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, responseHdr);
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
                    "transferFileData : Failed to transfer muliple chunks of data to host.\n");
                dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
                deleteAIOobjects(wInterface, responseHdr);
                return;
            }
        }
        rc = wInterface->transferDataHost(fd, data.offset, data.length,
                                          data.address, upstream);
        if (rc < 0)
        {
            error(
                "transferFileData : Failed to transfer single chunks of data to host.\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            deleteAIOobjects(wInterface, responseHdr);
            return;
        }
        if (static_cast<int>(data.length) == rc)
        {
            wInterface->setResponseReceived(true);
            dmaResponseToHost(responseHdr, PLDM_SUCCESS, origLength);
            if (responseHdr.functionPtr != nullptr)
            {
                responseHdr.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            deleteAIOobjects(wInterface, responseHdr);
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getNewXdmaFd();
        if (xdmaFd < 0)
        {
            error("transferFileData : Failed to get the XDMA file descriptor.");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, responseHdr);
            return;
        }
        if (xdmaInterface->initTimer(event, std::move(timerCb)) == false)
        {
            error("transferFileData : Failed to start the event timer.\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            deleteAIOobjects(xdmaInterface, responseHdr);
            return;
        }
        xdmaInterface->insertIOInstance(std::move(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback))));
    }
    catch (const std::runtime_error& e)
    {
        error(
            "transferFileData : Failed to start the event loop. error ={ERR_EXCEP} ",
            "ERR_EXCEP", e.what());
        dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
        deleteAIOobjects(xdmaInterface, responseHdr);
    }
}

void FileHandler::transferFileDataToSocket(int32_t fd, uint32_t& length,
                                           uint64_t address,
                                           ResponseHdr& responseHdr,
                                           sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    uint8_t command = responseHdr.command;
    if (nullptr == xdmaInterface)
    {
        error(
            "transferFileDataToSocket : xdma interface initialization failed.\n");
        dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
        if (responseHdr.functionPtr != nullptr)
        {
            responseHdr.functionPtr->postDataTransferCallBack(
                command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
        }
        deleteAIOobjects(nullptr, responseHdr);
        return;
    }
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    dma::FileMetaData data;
    data.length = length;
    data.address = address;
    xdmaInterface->setFileMetaData(data);
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    std::weak_ptr<FileHandler> wxfunctionPtr = responseHdr.functionPtr;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->getResponseReceived())
        {
            error(
                "EventLoop Timeout...Terminating socket data tranfer operation\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            if (responseHdr.functionPtr != nullptr)
            {
                responseHdr.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            deleteAIOobjects(xdmaInterface, responseHdr);
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
                    "transferFileDataToSocket : Failed to transfer muliple chunks of data to host.\n");
                dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
                if (responseHdr.functionPtr != nullptr)
                {
                    responseHdr.functionPtr->postDataTransferCallBack(
                        command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
                }
                deleteAIOobjects(wInterface, responseHdr);
                return;
            }
        }
        rc = wInterface->transferHostDataToSocket(fd, data.length,
                                                  data.address);
        if (rc < 0)
        {
            error(
                "transferFileDataToSocket : Failed to transfer single chunks of data to host.\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            if (responseHdr.functionPtr != nullptr)
            {
                responseHdr.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            deleteAIOobjects(wInterface, responseHdr);
            return;
        }
        if (static_cast<int>(data.length) == rc)
        {
            wInterface->setResponseReceived(true);
            dmaResponseToHost(responseHdr, PLDM_SUCCESS, origLength);
            deleteAIOobjects(wInterface, responseHdr);
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getNewXdmaFd();
        if (xdmaFd < 0)
        {
            error(
                "transferFileDataToSocket : Failed to open shared memory location.");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            if (responseHdr.functionPtr != nullptr)
            {
                responseHdr.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            deleteAIOobjects(xdmaInterface, responseHdr);
            return;
        }
        if (xdmaInterface->initTimer(event, std::move(timerCb)) == false)
        {
            error(
                "transferFileDataToSocket : Failed to start the event timer.\n");
            dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
            if (responseHdr.functionPtr != nullptr)
            {
                responseHdr.functionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            deleteAIOobjects(xdmaInterface, responseHdr);
            return;
        }
        xdmaInterface->insertIOInstance(std::move(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback))));
    }
    catch (const std::runtime_error& e)
    {
        error(
            "transferFileDataToSocket : Failed to start the event loop. error ={ERR_EXCEP} ",
            "ERR_EXCEP", e.what());
        dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
        if (responseHdr.functionPtr != nullptr)
        {
            responseHdr.functionPtr->postDataTransferCallBack(
                command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
        }
        deleteAIOobjects(xdmaInterface, responseHdr);
    }
    return;
}

void FileHandler::transferFileData(const fs::path& path, bool upstream,
                                   uint32_t offset, uint32_t& length,
                                   uint64_t address, ResponseHdr& responseHdr,
                                   sdeventplus::Event& event)
{
    bool fileExists = false;
    if (upstream)
    {
        fileExists = fs::exists(path);
        if (!fileExists)
        {
            error("File does not exist. PATH={PATH}", "PATH", path.c_str());
            dmaResponseToHost(responseHdr, PLDM_INVALID_FILE_HANDLE, length);
            deleteAIOobjects(nullptr, responseHdr);
            return;
        }

        size_t fileSize = fs::file_size(path);
        if (offset >= fileSize)
        {
            error(
                " transferFileData : FileHandler::transferFileData: Offset exceeds file size, OFFSET={OFFSET} FILE_SIZE={FILE_SIZE} FILE_HANDLE={FILE_HANDLE}",
                "OFFSET", offset, "FILE_SIZE", fileSize, "FILE_HANDLE",
                fileHandle);
            dmaResponseToHost(responseHdr, PLDM_DATA_OUT_OF_RANGE, length);
            deleteAIOobjects(nullptr, responseHdr);
            return;
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
    int file = open(path.string().c_str(), flags | O_NONBLOCK);
    if (file == -1)
    {
        error("File does not exist, PATH = {PATH}", "PATH", path.string());
        dmaResponseToHost(responseHdr, PLDM_ERROR, 0);
        deleteAIOobjects(nullptr, responseHdr);
        return;
    }
    pldm::utils::CustomFD fd(file, false);
    transferFileData(fd(), upstream, offset, length, address, responseHdr,
                     event);
}

std::unique_ptr<FileHandler> getHandlerByType(uint16_t fileType,
                                              uint32_t fileHandle)
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
        case PLDM_FILE_TYPE_LID_RUNNING:
        {
            return std::make_unique<LidHandler>(fileHandle, false,
                                                PLDM_FILE_TYPE_LID_RUNNING);
        }
        case PLDM_FILE_TYPE_DUMP:
        case PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS:
        case PLDM_FILE_TYPE_RESOURCE_DUMP:
        case PLDM_FILE_TYPE_BMC_DUMP:
        case PLDM_FILE_TYPE_SBE_DUMP:
        case PLDM_FILE_TYPE_HOSTBOOT_DUMP:
        case PLDM_FILE_TYPE_HARDWARE_DUMP:
        {
            return std::make_unique<DumpHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_CERT_SIGNING_REQUEST:
        case PLDM_FILE_TYPE_SIGNED_CERT:
        case PLDM_FILE_TYPE_ROOT_CERT:
        {
            return std::make_unique<CertHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_COD_LICENSE_KEY:
        case PLDM_FILE_TYPE_COD_LICENSED_RESOURCES:
        {
            return std::make_unique<LicenseHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PROGRESS_SRC:
        {
            return std::make_unique<ProgressCodeHandler>(fileHandle);
        }
        case PLDM_FILE_TYPE_PCIE_TOPOLOGY:
        case PLDM_FILE_TYPE_CABLE_INFO:
        {
            return std::make_unique<PCIeInfoHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PSPD_VPD_PDD_KEYWORD:
        {
            return std::make_unique<keywordHandler>(fileHandle, fileType);
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
        case PLDM_FILE_TYPE_HOSTBOOT_DUMP:
        case PLDM_FILE_TYPE_HARDWARE_DUMP:
        {
            return std::make_shared<DumpHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_CERT_SIGNING_REQUEST:
        case PLDM_FILE_TYPE_SIGNED_CERT:
        case PLDM_FILE_TYPE_ROOT_CERT:
        {
            return std::make_shared<CertHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_COD_LICENSE_KEY:
        case PLDM_FILE_TYPE_COD_LICENSED_RESOURCES:
        {
            return std::make_shared<LicenseHandler>(fileHandle, fileType);
        }
        case PLDM_FILE_TYPE_PROGRESS_SRC:
        {
            return std::make_shared<ProgressCodeHandler>(fileHandle);
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
        error("File does not exist, HANDLE={FILE_HNDLE} PATH={PATH}",
              "FILE_HNDLE", fileHandle, "PATH", filePath.c_str());
        return PLDM_INVALID_FILE_HANDLE;
    }

    size_t fileSize = fs::file_size(filePath);
    if (offset >= fileSize)
    {
        error(
            "FileHandler::readFileData: Offset exceeds file size, OFFSET={OFFSET} FILE_SIZE={FILE_SIZE} FILE_HANDLE={FILE_HANDLE}",
            "OFFSET", offset, "FILE_SIZE", fileSize, "FILE_HANDLE", fileHandle);
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
    error("Unable to read file, FILE={FILE_PATH}", "FILE_PATH",
          filePath.c_str());
    return PLDM_ERROR;
}

} // namespace responder
} // namespace pldm
