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
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>
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
using sdeventplus::source::IO;
using sdeventplus::source::Signal;
int FileHandler::transferFileData(int32_t file, bool upstream, uint32_t offset,
                                  uint32_t& length, uint64_t address,
                                  sdeventplus::Event& event)
{
    // static dma::DMA xdmaInterface(length);
    static dma::DMA* xdmaInterface = new dma::DMA(length);
    /*   while (length > dma::maxSize)
       {
           auto rc = xdmaInterface.transferDataHost(fd, offset, dma::maxSize,
                                                    address, upstream);
           if (rc < 0)
           {
               return PLDM_ERROR;
           }
           offset += dma::maxSize;
           length -= dma::maxSize;
           address += dma::maxSize;
       }
       auto rc =
           xdmaInterface.transferDataHost(fd, offset, length, address,
       upstream); return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
   */
    std::cout << "KK transferFileData trace 1 \n";
    // static pldm::utils::CustomFD fd(file);
    uint32_t origLength = length;
    // static auto& bus = pldm::utils::DBusHandler::getBus();
    // bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    static dma::IOPart part;
    part.length = length;
    part.offset = offset;
    part.address = address;
    static int rc = 0;
    std::cout << "KK t00 part.length:" << part.length
              << " part.offset:" << part.offset
              << " part.address:" << part.address
              << " origLength:" << origLength
              << " dma::maxSize:" << dma::maxSize << "\n";

    auto timerCb = [this](void) {
        std::cout
            << "KK timer callback called....xdmaInterface.responseReceived:"
            << xdmaInterface->responseReceived << "\n";
        if (!xdmaInterface->responseReceived)
        {
            std::cout << "KK inside respose received"
                      << "\n";
        }
    };
    auto timer =
        std::make_unique<phosphor::Timer>(event.get(), std::move(timerCb));
    try
    {
        std::cout << "KK timer callback setting\n";
        std::chrono::seconds eventloopExpiryInterval = std::chrono::seconds(1);
        timer->start(std::chrono::duration_cast<std::chrono::microseconds>(
            eventloopExpiryInterval));
    }
    catch (const std::runtime_error& e)
    {
        // requester.markFree(eid, instanceId);
        std::cerr << "Failed to start the event loop expiry timer. RC = "
                  << e.what() << "\n";
        rc = -1;
        return rc;
    }

    int xdmaFd = xdmaInterface->dmaFd(true, true);
    auto callback = [=, this](IO& io, int xdmaFd, uint32_t revents) {
        std::cout << "KK starts eventloop\n";
        if (!(revents & EPOLLIN))
        {
            std::cout << "KK retruning before evenloop start revents:"
                      << revents << "\n";
            return;
        }
        // sleep(10);
        std::cout << "KK t11 part.length:" << part.length
                  << " part.offset:" << part.offset
                  << " part.address:" << part.address
                  << " dma::maxSize:" << dma::maxSize << " revents:" << revents
                  << " EPOLLIN:" << EPOLLIN << "\n ";
        io.set_fd(xdmaFd);
        while (part.length > dma::maxSize)
        {
            rc = xdmaInterface->transferDataHost(
                file, part.offset, dma::maxSize, part.address, upstream);
            std::cout << "KK t22 part.length:" << part.length
                      << " part.offset:" << part.offset
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << "\n";
            part.length -= dma::maxSize;
            part.offset += dma::maxSize;
            part.address += dma::maxSize;
            std::cout << "KK t33 part.length:" << part.length
                      << " part.offset:" << part.offset
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << " rc:" << rc
                      << "\n";
            if (rc < 0)
            {
                // bus.detach_event();
                return;
            }
        }
        rc = xdmaInterface->transferDataHost(file, part.offset, part.length,
                                             part.address, upstream);
        std::cout << "KK t44 part.length:" << part.length
                  << " part.offset:" << part.offset
                  << " part.address:" << part.address << " rc:" << rc << "\n";
        if (rc < 0)
        {
            std::cout << "KK t55 transferFileData::transferDataHost error:"
                      << " rc:" << rc << "\n";
            // bus.detach_event();
            return;
        }
        xdmaInterface->responseReceived = true;
        // bus.detach_event();
        std::cout << "KK returning from event loop "
                  << "\n ";
        return;
    };
    static IO ioa(event, xdmaFd, EPOLLIN, std::move(callback));
    std::cout << "KK returning from transferFileData rc:" << rc << "\n";

    return rc;
}

int FileHandler::transferFileDataToSocket(int32_t fd, uint32_t& length,
                                          uint64_t address,
                                          sdeventplus::Event& event)
{
    std::cout << "KK starting  transferHostDataToSocket\n";
    static dma::DMA* xdmaInterface = new dma::DMA(length);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    static dma::IOPart part;
    part.length = length;
    part.address = address;
    static int rc = 0;
    std::cout << "KK t000 part.length:" << part.length
              << " part.address:" << part.address
              << " origLength:" << origLength
              << " dma::maxSize:" << dma::maxSize << "\n ";
    int xdmaFd = xdmaInterface->dmaFd(true, true);
    auto callback = [=](IO&, int, uint32_t revents) {
        if (!(revents & EPOLLIN))
        {
            return;
        }
        std::cout << "KK t111 part.length:" << part.length
                  << " part.address:" << part.address
                  << " dma::maxSize:" << dma::maxSize << " revents:" << revents
                  << " EPOLLIN:" << EPOLLIN << "\n ";
        while (part.length > dma::maxSize)
        {
            rc = xdmaInterface->transferHostDataToSocket(fd, dma::maxSize,
                                                         address);

            std::cout << "KK t222 part.length:" << part.length
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << "\n";
            part.length -= dma::maxSize;
            part.address += dma::maxSize;
            std::cout << "KK t333 part.length:" << part.length
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << " rc:" << rc
                      << "\n";
            if (rc < 0)
            {
                std::cout
                    << "KK t555 transferFileDataToSocket::transferDataHost error:"
                    << " rc:" << rc << "\n";
                // bus.detach_event();
                return;
            }
        }
        rc = xdmaInterface->transferHostDataToSocket(fd, dma::maxSize, address);

        std::cout << "KK t444 part.length:" << part.length
                  << " part.address:" << part.address << " rc:" << rc << "\n";
        if (rc < 0)
        {
            // bus.detach_event();
            return;
        }

        // bus.detach_event();
        return;
    };
    static IO ios(event, xdmaFd, EPOLLIN, std::move(callback));
    std::cout << "KK returning from transferHostDataToSocket rc:" << rc << "\n";
    return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
}

int FileHandler::transferFileData(const fs::path& path, bool upstream,
                                  uint32_t offset, uint32_t& length,
                                  uint64_t address, sdeventplus::Event& event)
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

    responder::CustomFDL fd(file);
    std::cout << "KK calling transferFileData with opened socket id:" << file
              << "\n ";
    return transferFileData(fd(), upstream, offset, length, address, event);
}

std::unique_ptr<FileHandler> getHandlerByType(uint16_t fileType,
                                              uint32_t fileHandle)
{
    std::cout << "KK printing filetype:" << fileType << "\n";
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
