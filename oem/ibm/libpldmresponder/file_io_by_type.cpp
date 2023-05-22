#include "config.h"

// #include "file_io_by_type.hpp"
#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include "common/utils.hpp"
#include "file_io.hpp"
#include "file_io_type_cert.hpp"
#include "file_io_type_dump.hpp"
#include "file_io_type_lic.hpp"
#include "file_io_type_lid.hpp"
#include "file_io_type_pcie.hpp"
#include "file_io_type_pel.hpp"
#include "file_io_type_progress_src.hpp"
#include "file_io_type_vpd.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

// #include <function2/function2.hpp>
//  #include <sdbusplus/bus.hpp>
//  #include <sdbusplus/server.hpp>
//  #include <sdbusplus/server/object.hpp>
//  #include <sdbusplus/timer.hpp>
//  #include <sdeventplus/clock.hpp>
//  #include <sdeventplus/event.hpp>
//  #include <sdeventplus/exception.hpp>
//  #include <sdeventplus/source/io.hpp>
//  #include <sdeventplus/source/signal.hpp>
//  #include <sdeventplus/source/time.hpp>
//  #include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <vector>
namespace pldm
{
namespace responder
{
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdeventplus;
using namespace sdeventplus::source;
// constexpr auto clockId = sdeventplus::ClockId::RealTime;
// using Clock = Clock<clockId>;

int FileHandler::transferFileData(int32_t fd, bool upstream, uint32_t offset,
                                  uint32_t& length, uint64_t address,
                                  ResponseHdr& responseHdr,
                                  sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    xdmaInterface->setDMASourceFd(fd);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    uint key = responseHdr.key;
    uint8_t instance_id = responseHdr.instance_id;
    uint8_t command = responseHdr.command;
    auto functionPtr = std::move(responseHdr.functionPtr);
    static dma::IOPart part;
    part.length = length;
    part.offset = offset;
    part.address = address;
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    std::weak_ptr<FileHandler> wxfunctionPtr = functionPtr;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->responseReceived)
        {
            std::cout
                << " EventLoop Timeout..!! Terminating FileHandler data tranfer operation.\n";
            Response response(sizeof(pldm_msg_hdr) + command, 0);
            auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
            encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR,
                                               0, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);
            xdmaInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(functionPtr)).reset();
        }
        return;
    };

    auto callback = [=, this](IO&, int, uint32_t revents) {
        if (!(revents & (EPOLLIN | EPOLLOUT)))
        {
            return;
        }
        auto wInterface = wxInterface.lock();
        auto wfunctionPtr = wxfunctionPtr.lock();
        int rc = 0;
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

        while (part.length > dma::maxSize)
        {
            rc = wInterface->transferDataHost(fd, part.offset, dma::maxSize,
                                              part.address, upstream);
            part.length -= dma::maxSize;
            part.offset += dma::maxSize;
            part.address += dma::maxSize;
            if (rc < 0)
            {
                encode_rw_file_by_type_memory_resp(instance_id, command,
                                                   PLDM_ERROR, 0, responsePtr);
                responseHdr.respInterface->sendPLDMRespMsg(response, key);
                wInterface->deleteIOInstance();
                (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
                (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr))
                    .reset();
                return;
            }
        }
        rc = wInterface->transferDataHost(fd, part.offset, part.length,
                                          part.address, upstream);
        if (rc < 0)
        {
            encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR,
                                               0, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);
            wInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr)).reset();
            return;
        }
        if (static_cast<int>(part.length) == rc)
        {
            wInterface->responseReceived = true;
            encode_rw_file_by_type_memory_resp(
                instance_id, command, PLDM_SUCCESS, origLength, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);

            if (wfunctionPtr != nullptr)
            {
                wfunctionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            wInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr)).reset();
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getDMAFd(true, true);
        xdmaInterface->initTimer(event, std::move(timerCb));
        xdmaInterface->insertIOInstance(std::move(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback))));
    }
    catch (const std::runtime_error& e)
    {
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        std::cerr << "Failed to start the event loop. RC = " << e.what()
                  << "\n";
        encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                           responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response, key);
        xdmaInterface->deleteIOInstance();
        (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
        (static_cast<std::shared_ptr<FileHandler>>(functionPtr)).reset();

        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

int FileHandler::transferFileDataToSocket(int32_t fd, uint32_t& length,
                                          uint64_t address,
                                          ResponseHdr& responseHdr,
                                          sdeventplus::Event& event)
{
    std::shared_ptr<dma::DMA> xdmaInterface =
        std::make_shared<dma::DMA>(length);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    uint key = responseHdr.key;
    uint8_t instance_id = responseHdr.instance_id;
    uint8_t command = responseHdr.command;
    auto functionPtr = std::move(responseHdr.functionPtr);
    static dma::IOPart part;
    part.length = length;
    part.address = address;
    std::weak_ptr<dma::DMA> wxInterface = xdmaInterface;
    std::weak_ptr<FileHandler> wxfunctionPtr = functionPtr;
    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->responseReceived)
        {
            Response response(sizeof(pldm_msg_hdr) + command, 0);
            auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
            encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR,
                                               0, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);
            std::cout
                << "EventLoop Timeout...Terminating socket data tranfer operation\n";
            xdmaInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(functionPtr)).reset();
        }
        return;
    };
    auto callback = [=, this](IO&, int, uint32_t revents) {
        if (!(revents & (EPOLLIN | EPOLLOUT)))
        {
            return;
        }
        auto wInterface = wxInterface.lock();
        auto wfunctionPtr = wxfunctionPtr.lock();
        int rc = 0;
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        while (part.length > dma::maxSize)
        {
            rc = wInterface->transferHostDataToSocket(fd, dma::maxSize,
                                                      part.address);
            part.length -= dma::maxSize;
            part.address += dma::maxSize;
            if (rc < 0)
            {
                encode_rw_file_by_type_memory_resp(instance_id, command,
                                                   PLDM_ERROR, 0, responsePtr);
                responseHdr.respInterface->sendPLDMRespMsg(response, key);
                wInterface->deleteIOInstance();
                (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
                (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr))
                    .reset();
                return;
            }
        }
        rc = wInterface->transferHostDataToSocket(fd, part.length,
                                                  part.address);
        if (rc < 0)
        {
            encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR,
                                               0, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);
            wInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr)).reset();
            return;
        }
        if (static_cast<int>(part.length) == rc)
        {
            wInterface->responseReceived = true;
            encode_rw_file_by_type_memory_resp(
                instance_id, command, PLDM_SUCCESS, origLength, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response, key);
            if (wfunctionPtr != nullptr)
            {
                wfunctionPtr->postDataTransferCallBack(
                    command == PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY);
            }
            wInterface->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            (static_cast<std::shared_ptr<FileHandler>>(wfunctionPtr)).reset();
            return;
        }
    };
    try
    {
        int xdmaFd = xdmaInterface->getDMAFd(true, true);
        xdmaInterface->initTimer(event, std::move(timerCb));
        xdmaInterface->insertIOInstance(std::move(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback))));
    }
    catch (const std::runtime_error& e)
    {
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        std::cerr << "Failed to start socket the event loop. RC = " << e.what()
                  << "\n";
        encode_rw_file_by_type_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                           responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response, key);
        xdmaInterface->deleteIOInstance();
        (static_cast<std::shared_ptr<dma::DMA>>(xdmaInterface)).reset();
        (static_cast<std::shared_ptr<FileHandler>>(functionPtr)).reset();
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

int FileHandler::transferFileData(const fs::path& path, bool upstream,
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
            std::cerr << "File does not exist. PATH=" << path.c_str() << "\n";
            return PLDM_INVALID_FILE_HANDLE;
        }

        size_t fileSize = fs::file_size(path);
        if (offset >= fileSize)
        {
            std::cerr << "Offset exceeds file size, OFFSET=" << offset
                      << " FILE_SIZE=" << fileSize << "\n";
            return PLDM_DATA_OUT_OF_RANGE;
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
        std::cerr << "File does not exist, PATH = " << path.string() << "\n";
        return PLDM_ERROR;
    }
    pldm::utils::CustomFD fd(file, false);
    return transferFileData(fd(), upstream, offset, length, address,
                            responseHdr, event);
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
        std::cerr << "File does not exist, HANDLE=" << fileHandle
                  << " PATH=" << filePath.c_str() << "\n";
        return PLDM_INVALID_FILE_HANDLE;
    }

    size_t fileSize = fs::file_size(filePath);
    if (offset >= fileSize)
    {
        std::cerr << "Offset exceeds file size, OFFSET=" << offset
                  << " FILE_SIZE=" << fileSize << "\n";
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
    std::cerr << "Unable to read file, FILE=" << filePath.c_str() << "\n";
    return PLDM_ERROR;
}

} // namespace responder
} // namespace pldm
