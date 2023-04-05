#pragma once

#include "config.h"

#include "libpldm/base.h"
#include "libpldm/file_io.h"
#include "libpldm/host.h"

#include "common/utils.hpp"
#include "oem/ibm/requester/dbus_to_file_handler.hpp"
#include "oem_ibm_handler.hpp"
#include "pldmd/handler.hpp"
#include "requester/handler.hpp"

#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <function2/function2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdbusplus/timer.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>

#include <filesystem>
#include <iostream>
#include <vector>
namespace pldm
{
namespace responder
{
namespace dma
{

struct CustomFDL
{
    CustomFDL(const CustomFDL&) = delete;
    CustomFDL& operator=(const CustomFDL&) = delete;
    CustomFDL(CustomFDL&&) = delete;
    CustomFDL& operator=(CustomFDL&&) = delete;

    CustomFDL(int fd) : fd(fd)
    {}

    ~CustomFDL()
    {
        if (fd >= 0)
        {
            std::cout << "KK closing socket from custom local fd:" << fd
                      << "\n";
            close(fd);
        }
    }

    int operator()() const
    {
        return fd;
    }

  private:
    int fd = -1;
};

struct IOPart
{
    uint32_t length;
    uint32_t offset;
    uint64_t address;
};

// The minimum data size of dma transfer in bytes
constexpr uint32_t minSize = 16;

constexpr size_t maxSize = DMA_MAXSIZE;

namespace fs = std::filesystem;
using namespace sdeventplus;
using sdeventplus::Clock;
using sdeventplus::ClockId;
// using sdeventplus::Event;
using sdeventplus::source::IO;
using sdeventplus::source::Signal;
using namespace sdeventplus::source;
constexpr auto clockId = ClockId::RealTime;
using Timer = sdeventplus::utility::Timer<clockId>;
/**
 * @class DMA
 *
 * Expose API to initiate transfer of data by DMA
 *
 * This class only exposes the public API transferDataHost to transfer data
 * between BMC and host using DMA. This allows for mocking the transferDataHost
 * for unit testing purposes.
 */
class DMA
{
  public:
    DMA()
    {
        responseReceived = false;
    }
    DMA(uint32_t length)
    {
        std::cout << "KK DMA constructor called\n";
        responseReceived = false;
        m_length = length;
        static const size_t pageSize = getpagesize();
        uint32_t numPages = m_length / pageSize;
        pageAlignedLength = numPages * pageSize;
        if (m_length > pageAlignedLength)
        {
            pageAlignedLength += pageSize;
        }
    }

    ~DMA()
    {
        std::cout << "KK DMA destructor called\n";
        if (addr && addr != MAP_FAILED)
        {
            std::cout << "KK deleting memory from destructor\n";
            munmap(addr, pageAlignedLength);
            addr = nullptr;
        }
        if (xdmaFd != -1)
        {
            std::cout << "KK closing from destructor socket xdmaFd:" << xdmaFd;
            close(xdmaFd);
            xdmaFd = -1;
        }
        if (ioPtr != nullptr)
        {
            delete ioPtr;
            ioPtr = nullptr;
        }
    }

    int dmaFd(bool nonBlokingMode, bool Reopensocket)
    {
        if (Reopensocket)
        {
            std::cout << "KK closing existing opened socket xdmaFd:" << xdmaFd
                      << "\n";
            close(xdmaFd);
            xdmaFd = -1;
        }
        if (xdmaFd > 0)
        {
            return xdmaFd;
        }
        if (nonBlokingMode)
        {
            xdmaFd = open("/dev/aspeed-xdma", O_RDWR | O_NONBLOCK);
        }
        else
        {
            xdmaFd = open("/dev/aspeed-xdma", O_RDWR);
        }
        std::cout << "KK opening socket xdmaFd:" << xdmaFd
                  << " nonBlokingMode:" << nonBlokingMode << "\n";
        if (xdmaFd < 0)
        {
            rc = -errno;
        }
        return xdmaFd;
    }

    int32_t getpageAlignedLength()
    {
        return pageAlignedLength;
    }
    void* dmaAddr()
    {
        std::cout << "KK allocating memory size pageAlignedLength:"
                  << pageAlignedLength << "\n";
        addr = mmap(nullptr, pageAlignedLength, PROT_WRITE | PROT_READ,
                    MAP_SHARED, xdmaFd, 0);
        if (MAP_FAILED == addr)
        {
            rc = -errno;
            return MAP_FAILED;
        }
        return addr;
    }

    int error()
    {
        return rc;
    }

    /** @brief API to transfer data between BMC and host using DMA
     *
     * @param[in] path     - pathname of the file to transfer data from or
     * to
     * @param[in] offset   - offset in the file
     * @param[in] length   - length of the data to transfer
     * @param[in] address  - DMA address on the host
     * @param[in] upstream - indicates direction of the transfer; true
     * indicates transfer to the host
     *
     * @return returns 0 on success, negative errno on failure
     */
    int transferDataHost(int fd, uint32_t offset, uint32_t length,
                         uint64_t address, bool upstream);

    /** @brief API to transfer data on to unix socket from host using DMA
     *
     * @param[in] path     - pathname of the file to transfer data from or
     * to
     * @param[in] length   - length of the data to transfer
     * @param[in] address  - DMA address on the host
     *
     * @return returns 0 on success, negative errno on failure
     */
    int transferHostDataToSocket(int fd, uint32_t length, uint64_t address);
    IO* ioPtr = nullptr;
    bool responseReceived = false;

  private:
    int xdmaFd = -1;
    void* addr = nullptr;
    int rc = 0;
    uint32_t pageAlignedLength = 0;
    uint32_t m_length;
};

/** @brief Transfer the data between BMC and host using DMA.
 *
 *  There is a max size for each DMA operation, transferAll API abstracts this
 *  and the requested length is broken down into multiple DMA operations if the
 *  length exceed max size.
 *
 * @tparam[in] T - DMA interface type
 * @param[in] intf - interface passed to invoke DMA transfer
 * @param[in] command  - PLDM command
 * @param[in] path     - pathname of the file to transfer data from or to
 * @param[in] offset   - offset in the file
 * @param[in] length   - length of the data to transfer
 * @param[in] address  - DMA address on the host
 * @param[in] upstream - indicates direction of the transfer; true indicates
 *                       transfer to the host
 * @param[in] instanceId - Message's instance id
 * @return PLDM response message
 */

// template <class DMAInterface>
/*Response transferAll(DMAInterface* intf, uint8_t command, fs::path& path,
                     uint32_t offset, uint32_t length, uint64_t address,
                     bool upstream, uint8_t instanceId)
{
    uint32_t origLength = length;
    Response response(sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_RESP_BYTES, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    int flags{};
    if (upstream)
    {
        flags = O_RDONLY;
    }
    else if (fs::exists(path))
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
        std::cerr << "File does not exist, path = " << path.string() << "\n";
        encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                   responsePtr);
        return response;
    }
    pldm::utils::CustomFD fd(file);

    while (length > dma::maxSize)
    {
        auto rc = intf->transferDataHost(fd(), offset, dma::maxSize, address,
                                         upstream);
        if (rc < 0)
        {
            encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                       responsePtr);
            return response;
        }

        offset += dma::maxSize;
        length -= dma::maxSize;
        address += dma::maxSize;
    }

    auto rc = intf->transferDataHost(fd(), offset, length, address, upstream);
    if (rc < 0)
    {
        encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                   responsePtr);
        return response;
    }

    encode_rw_file_memory_resp(instanceId, command, PLDM_SUCCESS, origLength,
                               responsePtr);
    return response;
}*/
template <class DMAInterface>
Response transferAll(DMAInterface* intf, uint8_t command, fs::path& path,
                     uint32_t offset, uint32_t length, uint64_t address,
                     bool upstream, uint8_t instanceId,
                     sdeventplus::Event& event)
{
    Response response(sizeof(pldm_msg_hdr) + command, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    std::cout << "KK transferAll calling...\n";
    int flags{};
    if (upstream)
    {
        flags = O_RDONLY;
    }
    else if (fs::exists(path))
    {
        flags = O_RDWR;
    }
    else
    {
        flags = O_WRONLY;
    }
    std::cout << "KK transferAll trace 0 \n";
    static int file = open(path.string().c_str(), O_NONBLOCK | flags);
    if (file == -1)
    {
        std::cerr << "File does not exist, path = " << path.string() << "\n";
        encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                   responsePtr);
        return response;
    }
    std::cout << "KK transferAll trace 1 \n";
    static CustomFDL fd(file); // = new CustomFDL(file);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    static IOPart part;
    part.length = length;
    part.offset = offset;
    part.address = address;
    std::cout << "KK t0 part.length:" << part.length
              << " part.offset:" << part.offset
              << " part.address:" << part.address
              << " origLength:" << origLength
              << " dma::maxSize:" << dma::maxSize << "\n ";

    auto timerCb = [&](dma::Timer&) {
        std::cout
            << "KK timer callback called....xdmaInterface.responseReceived:"
            << intf->responseReceived << "\n";
        if (!intf->responseReceived)
        {
            std::cout << "KK inside respose received"
                      << "\n";
        }
    };
    dma::Timer timer(event, std::move(timerCb), std::chrono::seconds{5});

    int xdmaFd = intf->dmaFd(true, true);
    auto callback = [=](IO& io, int xdmaFd, uint32_t revents) {
        if (!(revents & (EPOLLIN)))
        {
            return;
        }
        std::cout << "KK t1 part.length:" << part.length
                  << " part.offset:" << part.offset
                  << " part.address:" << part.address
                  << " dma::maxSize:" << dma::maxSize << " revents:" << revents
                  << " EPOLLIN:" << EPOLLIN << "\n ";
        io.set_fd(xdmaFd);
        while (part.length > dma::maxSize)
        {
            auto rc = intf->transferDataHost(fd(), part.offset, dma::maxSize,
                                             part.address, upstream);

            std::cout << "KK t2 part.length:" << part.length
                      << " part.offset:" << part.offset
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << "\n";
            part.length -= dma::maxSize;
            part.offset += dma::maxSize;
            part.address += dma::maxSize;
            std::cout << "KK t3 part.length:" << part.length
                      << " part.offset:" << part.offset
                      << " part.address:" << part.address
                      << " dma::maxSize:" << dma::maxSize << " rc:" << rc
                      << "\n";
            if (rc < 0)
            {
                encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                           responsePtr);
                // bus.detach_event();
                return;
            }
        }
        auto rc = intf->transferDataHost(fd(), part.offset, part.length,
                                         part.address, upstream);
        std::cout << "KK t4 part.length:" << part.length
                  << " part.offset:" << part.offset
                  << " part.address:" << part.address << " rc:" << rc << "\n";
        if (rc < 0)
        {
            encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                       responsePtr);
            // bus.detach_event();
            return;
        }
        intf->responseReceived = true;
        encode_rw_file_memory_resp(instanceId, command, PLDM_SUCCESS,
                                   origLength, responsePtr);
        // bus.detach_event();
        return;
    };
    static IO io(event, xdmaFd, EPOLLIN, std::move(callback));

    encode_rw_file_memory_resp(instanceId, command, PLDM_SUCCESS, 0,
                               responsePtr);
    std::cout << "KK returning from transferAll end"
              << "\n";

    return response;
}

} // namespace dma

namespace oem_ibm
{
static constexpr auto dumpObjPath = "/xyz/openbmc_project/dump/resource/entry/";
static constexpr auto resDumpEntry = "com.ibm.Dump.Entry.Resource";

static constexpr auto certObjPath = "/xyz/openbmc_project/certs/ca/";
static constexpr auto certAuthority =
    "xyz.openbmc_project.PLDM.Provider.Certs.Authority.CSR";

static constexpr auto codLicObjPath = "/com/ibm/license";
static constexpr auto codLicInterface = "com.ibm.License.LicenseManager";
class Handler : public CmdHandler
{
  public:
    Handler(oem_platform::Handler* oemPlatformHandler, int hostSockFd,
            uint8_t hostEid, dbus_api::Requester* dbusImplReqester,
            pldm::requester::Handler<pldm::requester::Request>* handler) :
        oemPlatformHandler(oemPlatformHandler),
        hostSockFd(hostSockFd), hostEid(hostEid),
        dbusImplReqester(dbusImplReqester), handler(handler),
        event(sdeventplus::Event::get_default())
    {
        handlers.emplace(PLDM_READ_FILE_INTO_MEMORY,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->readFileIntoMemory(request,
                                                             payloadLength);
                         });
        handlers.emplace(PLDM_WRITE_FILE_FROM_MEMORY,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->writeFileFromMemory(request,
                                                              payloadLength);
                         });
        handlers.emplace(PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->writeFileByTypeFromMemory(
                                 request, payloadLength);
                         });
        handlers.emplace(PLDM_READ_FILE_BY_TYPE_INTO_MEMORY,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->readFileByTypeIntoMemory(
                                 request, payloadLength);
                         });
        handlers.emplace(PLDM_READ_FILE_BY_TYPE, [this](const pldm_msg* request,
                                                        size_t payloadLength) {
            return this->readFileByType(request, payloadLength);
        });
        handlers.emplace(PLDM_WRITE_FILE_BY_TYPE,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->writeFileByType(request,
                                                          payloadLength);
                         });
        handlers.emplace(PLDM_GET_FILE_TABLE,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->getFileTable(request, payloadLength);
                         });
        handlers.emplace(PLDM_READ_FILE,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->readFile(request, payloadLength);
                         });
        handlers.emplace(PLDM_WRITE_FILE,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->writeFile(request, payloadLength);
                         });
        handlers.emplace(PLDM_FILE_ACK,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->fileAck(request, payloadLength);
                         });
        handlers.emplace(PLDM_HOST_GET_ALERT_STATUS,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->getAlertStatus(request,
                                                         payloadLength);
                         });
        handlers.emplace(PLDM_NEW_FILE_AVAILABLE,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->newFileAvailable(request,
                                                           payloadLength);
                         });
        handlers.emplace(PLDM_FILE_ACK_WITH_META_DATA,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->fileAckWithMetaData(request,
                                                              payloadLength);
                         });

        handlers.emplace(PLDM_NEW_FILE_AVAILABLE_WITH_META_DATA,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->newFileAvailableWithMetaData(
                                 request, payloadLength);
                         });

        resDumpMatcher = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, dumpObjPath),
            [this, hostSockFd, hostEid, dbusImplReqester,
             handler](sdbusplus::message::message& msg) {
                std::map<
                    std::string,
                    std::map<std::string, std::variant<std::string, uint32_t>>>
                    interfaces;
                sdbusplus::message::object_path path;
                msg.read(path, interfaces);
                std::string vspstring;
                std::string password;

                for (auto& interface : interfaces)
                {
                    if (interface.first == resDumpEntry)
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "VSPString")
                            {
                                vspstring =
                                    std::get<std::string>(property.second);
                            }
                            else if (property.first == "Password")
                            {
                                password =
                                    std::get<std::string>(property.second);
                            }
                        }
                        dbusToFileHandlers
                            .emplace_back(
                                std::make_unique<pldm::requester::oem_ibm::
                                                     DbusToFileHandler>(
                                    hostSockFd, hostEid, dbusImplReqester, path,
                                    handler))
                            ->processNewResourceDump(vspstring, password);
                        break;
                    }
                }
            });
        vmiCertMatcher = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, certObjPath),
            [this, hostSockFd, hostEid, dbusImplReqester,
             handler](sdbusplus::message::message& msg) {
                std::map<
                    std::string,
                    std::map<std::string, std::variant<std::string, uint32_t>>>
                    interfaces;
                sdbusplus::message::object_path path;
                msg.read(path, interfaces);
                std::string csr;

                for (auto& interface : interfaces)
                {
                    if (interface.first == certAuthority)
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "CSR")
                            {
                                csr = std::get<std::string>(property.second);
                                auto fileHandle =
                                    sdbusplus::message::object_path(path)
                                        .filename();

                                dbusToFileHandlers
                                    .emplace_back(std::make_unique<
                                                  pldm::requester::oem_ibm::
                                                      DbusToFileHandler>(
                                        hostSockFd, hostEid, dbusImplReqester,
                                        path, handler))
                                    ->newCsrFileAvailable(csr, fileHandle);
                                break;
                            }
                        }
                        break;
                    }
                }
            });
        codLicensesubs = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::propertiesChanged(codLicObjPath,
                                                            codLicInterface),
            [this, hostSockFd, hostEid, dbusImplReqester,
             handler](sdbusplus::message::message& msg) {
                sdbusplus::message::object_path path;
                std::map<dbus::Property, pldm::utils::PropertyValue> props;
                std::string iface;
                msg.read(iface, props);
                std::string licenseStr;

                for (auto& prop : props)
                {
                    if (prop.first == "LicenseString")
                    {
                        pldm::utils::PropertyValue licStrVal{prop.second};
                        licenseStr = std::get<std::string>(licStrVal);
                        if (licenseStr.empty())
                        {
                            return;
                        }
                        dbusToFileHandlers
                            .emplace_back(
                                std::make_unique<pldm::requester::oem_ibm::
                                                     DbusToFileHandler>(
                                    hostSockFd, hostEid, dbusImplReqester, path,
                                    handler))
                            ->newLicFileAvailable(licenseStr);
                        break;
                    }
                    break;
                }
            });
    }

    /** @brief Handler for readFileIntoMemory command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message
     *
     *  @return PLDM response message
     */
    Response readFileIntoMemory(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for writeFileIntoMemory command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message
     *
     *  @return PLDM response message
     */
    Response writeFileFromMemory(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for writeFileByTypeFromMemory command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message
     *
     *  @return PLDM response message
     */

    Response writeFileByTypeFromMemory(const pldm_msg* request,
                                       size_t payloadLength);

    /** @brief Handler for readFileByTypeIntoMemory command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message
     *
     *  @return PLDM response message
     */
    Response readFileByTypeIntoMemory(const pldm_msg* request,
                                      size_t payloadLength);

    /** @brief Handler for writeFileByType command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message
     *
     *  @return PLDM response message
     */
    Response readFileByType(const pldm_msg* request, size_t payloadLength);

    Response writeFileByType(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for GetFileTable command
     *
     *  @param[in] request - pointer to PLDM request payload
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response getFileTable(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for readFile command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response readFile(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for writeFile command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response writeFile(const pldm_msg* request, size_t payloadLength);

    Response fileAck(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for getAlertStatus command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response getAlertStatus(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for newFileAvailable command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response newFileAvailable(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for fileAckWithMetaData command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *   @return PLDM response message
     */
    Response fileAckWithMetaData(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for newFileAvailableWithMetaData command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response messsage
     */
    Response newFileAvailableWithMetaData(const pldm_msg* request,
                                          size_t payloadLength);

  private:
    oem_platform::Handler* oemPlatformHandler;
    int hostSockFd;
    uint8_t hostEid;
    dbus_api::Requester* dbusImplReqester;
    using DBusInterfaceAdded = std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::variant<std::string>>>>>;
    std::unique_ptr<pldm::requester::oem_ibm::DbusToFileHandler>
        dbusToFileHandler; //!< pointer to send request to Host
    std::unique_ptr<sdbusplus::bus::match::match>
        resDumpMatcher; //!< Pointer to capture the interface added signal
                        //!< for new resource dump
    std::unique_ptr<sdbusplus::bus::match::match>
        vmiCertMatcher; //!< Pointer to capture the interface added signal
                        //!< for new csr string
    std::unique_ptr<sdbusplus::bus::match::match>
        codLicensesubs; //!< Pointer to capture the property changed signal
                        //!< for new license string
    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;
    std::vector<std::unique_ptr<pldm::requester::oem_ibm::DbusToFileHandler>>
        dbusToFileHandlers;
    sdeventplus::Event event;
};

} // namespace oem_ibm
} // namespace responder
} // namespace pldm
