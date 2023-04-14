#include "config.h"

#include "file_io_by_type.hpp"

#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include "common/utils.hpp"
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
#include <unistd.h>

#include <function2/function2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdbusplus/timer.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>
#include <sdeventplus/source/time.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace pldm
{
namespace responder
{
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdeventplus;
using namespace sdeventplus::source;
using Timer = Time<clockId>;

int FileHandler::transferFileData(int32_t file, bool upstream, uint32_t offset,
                                  uint32_t& length, uint64_t address,
                                  ResponseHdr& responseHdr,
                                  sdeventplus::Event& event)
{
    static dma::DMA* xdmaInterface = new dma::DMA(length);
    xdmaInterface->setDMASourceFd(file);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    static dma::IOPart part;
    part.length = length;
    part.offset = offset;
    part.address = address;
    static int rc = 0;

    auto timerCb = [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!xdmaInterface->responseReceived)
        {
            std::cerr
                << "Failed to Complete the DMA transfer, Event loop timeout... \n";
            if (xdmaInterface != nullptr)
            {
                delete xdmaInterface;
                xdmaInterface = nullptr;
            }
        }
    };

    auto callback = [=, this](IO& io, int xdmaFd, uint32_t revents) {
        if (!(revents & EPOLLIN))
        {
            return;
        }
        Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        io.set_fd(xdmaFd);
        while (part.length > dma::maxSize)
        {
            rc = xdmaInterface->transferDataHost(
                file, part.offset, dma::maxSize, part.address, upstream);
            part.length -= dma::maxSize;
            part.offset += dma::maxSize;
            part.address += dma::maxSize;
            if (rc < 0)
            {
                encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                                   responseHdr.command,
                                                   PLDM_ERROR, 0, responsePtr);
                responseHdr.respInterface->sendPLDMRespMsg(response);
                return;
            }
        }
        rc = xdmaInterface->transferDataHost(file, part.offset, part.length,
                                             part.address, upstream);
        if (rc < 0)
        {
            encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                               responseHdr.command, PLDM_ERROR,
                                               0, responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response);
            return;
        }
        xdmaInterface->responseReceived = true;
        encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                           responseHdr.command, PLDM_SUCCESS,
                                           origLength, responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response);
        return;
    };
    try
    {
        int xdmaFd = xdmaInterface->DMASocketFd(true, true);
        static Timer time(event,
                          (Clock(event).now() + std::chrono::seconds{10}),
                          std::chrono::seconds{1}, std::move(timerCb));

        static IO ioa(event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback));
    }
    catch (const std::runtime_error& e)
    {
        Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        std::cerr << "Failed to start the event loop. RC = " << e.what()
                  << "\n";
        rc = -1;
        if (xdmaInterface != nullptr)
        {
            delete xdmaInterface;
            xdmaInterface = nullptr;
        }
        encode_rw_file_by_type_memory_resp(responseHdr.instance_id,
                                           responseHdr.command, PLDM_ERROR, 0,
                                           responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response);
        return -1;
    }
    return -1;
}

int FileHandler::transferFileDataToSocket(int32_t fd, uint32_t& length,
                                          uint64_t address)
{
    dma::DMA xdmaInterface;
    while (length > dma::maxSize)
    {
        auto rc =
            xdmaInterface.transferHostDataToSocket(fd, dma::maxSize, address);
        if (rc < 0)
        {
            return PLDM_ERROR;
        }
        length -= dma::maxSize;
        address += dma::maxSize;
    }
    auto rc = xdmaInterface.transferHostDataToSocket(fd, length, address);
    return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
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
    return transferFileData(file, upstream, offset, length, address,
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
