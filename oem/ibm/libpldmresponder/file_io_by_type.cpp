#include "config.h"

#include "file_io_by_type.hpp"

#include "libpldm/base.h"
#include "oem/ibm/libpldm/file_io.h"

#include "common/utils.hpp"
#include "file_io_type_cert.hpp"
#include "file_io_type_dump.hpp"
#include "file_io_type_lic.hpp"
#include "file_io_type_lid.hpp"
#include "file_io_type_pcie.hpp"
#include "file_io_type_pel.hpp"
#include "file_io_type_progress_src.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <stdint.h>
#include <unistd.h>

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

int FileHandler::transferFileData(int32_t fd, bool upstream, uint32_t offset,
                                  uint32_t& length, uint64_t address)
{
    dma::DMA xdmaInterface;
    while (length > dma::maxSize)
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
        xdmaInterface.transferDataHost(fd, offset, length, address, upstream);
    return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
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
                                  uint64_t address)
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
    int file = open(path.string().c_str(), flags);
    if (file == -1)
    {
        std::cerr << "File does not exist, PATH = " << path.string() << "\n";
        ;
        return PLDM_ERROR;
    }
    pldm::utils::CustomFD fd(file);

    return transferFileData(fd(), upstream, offset, length, address);
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

void FileHandler::sendResponse(bool verbose, uint8_t eid, int fd,
                               Response response)
{
    std::cerr << "Sending Response call\n";

    socklen_t optlen;
    int currentSendBuffSize;

    // Get Current send buffer size
    optlen = sizeof(currentSendBuffSize);

    int res =
        getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &currentSendBuffSize, &optlen);
    if (res == -1)
    {
        std::cerr << "Error calling setsockopt. RC = " << res
                  << ", errno = " << errno << std::endl;
    }
    struct msghdr msg
    {};
    // Outgoing message.
    struct iovec iov[2]{};

    if (verbose)
    {
        printBuffer(Tx, response);
    }
    std::vector<uint8_t> requestMsg{eid, 1};
    iov[0].iov_base = &requestMsg[0];
    iov[0].iov_len = sizeof(requestMsg[0]) + sizeof(requestMsg[1]);
    iov[1].iov_base = (response).data();
    iov[1].iov_len = (response).size();

    msg.msg_iov = iov;
    msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);
    if (currentSendBuffSize >= 0 &&
        (size_t)currentSendBuffSize < response.size())
    {
        currentSendBuffSize = (response).size();
        int res = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &currentSendBuffSize,
                             sizeof(currentSendBuffSize));
        if (res == -1)
            std::cerr << "Tx: Error calling setsockopt. RC = " << res
                      << ", errno = " << errno << std::endl;
    }
    std::cerr << "Sending Async Response\n";
    int result = sendmsg(fd, &msg, 0);
    if (-1 == result)
    {
        std::cerr << "sendto system call failed\n ";
    }
}

} // namespace responder
} // namespace pldm
