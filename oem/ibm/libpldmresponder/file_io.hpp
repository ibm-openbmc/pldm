#pragma once

#include "config.h"

#include "libpldm/base.h"
#include "libpldm/file_io.h"
#include "libpldm/host.h"

#include "common/utils.hpp"
#include "oem/ibm/requester/dbus_to_file_handler.hpp"
#include "oem_ibm_handler.hpp"
#include "pldmd/PldmRespInterface.hpp"
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
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>
#include <sdeventplus/source/time.hpp>

#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <vector>
namespace pldm
{
namespace responder
{
using namespace sdeventplus;
using namespace sdeventplus::source;
constexpr auto clockId = sdeventplus::ClockId::RealTime;
using Clock = Clock<clockId>;
using Timer = Time<clockId>;
constexpr auto xdmaDev = "/dev/aspeed-xdma";

struct ResponseHdr
{
    uint8_t instance_id;
    uint8_t command;
    pldm::Response_api::Transport* respInterface;
};

namespace dma
{

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
        responseReceived = false;
        static const size_t pageSize = getpagesize();
        uint32_t numPages = length / pageSize;
        pageAlignedLength = numPages * pageSize;
        if (length > pageAlignedLength)
        {
            pageAlignedLength += pageSize;
        }
        xdmaFd = -1;
        memAddr = nullptr;
        rc = 0;
        pageAlignedLength = 0;
        SourceFd = -1;
    }
    ~DMA()
    {
        if (xdmaFd >= 0)
        {
            close(xdmaFd);
        }
        if (SourceFd >= 0)
        {
            close(SourceFd);
        }
    }

    int DMASocketFd(bool nonBlokingMode, bool reOpenSocket)
    {
        if (reOpenSocket)
        {
            close(xdmaFd);
            xdmaFd = -1;
        }
        else
        {
            return xdmaFd;
        }
        if (nonBlokingMode)
        {
            xdmaFd = open(xdmaDev, O_RDWR | O_NONBLOCK);
        }
        else
        {
            xdmaFd = open(xdmaDev, O_RDWR);
        }
        if (xdmaFd < 0)
        {
            rc = -errno;
            return rc;
        }
        return xdmaFd;
    }

    /// @brief function will keep one copy of fd for exception so it can close
    /// it.
    /// @param fd source path file descriptor
    void setDMASourceFd(int fd)
    {
        SourceFd = fd;
    }

    int32_t getpageAlignedLength()
    {
        return pageAlignedLength;
    }
    ///
    /// @brief function will return mapped memory address
    /// from XDMA drive path
    void* DMAMemAddr()
    {
        memAddr = mmap(nullptr, pageAlignedLength, PROT_WRITE | PROT_READ,
                       MAP_SHARED, xdmaFd, 0);
        if (MAP_FAILED == memAddr)
        {
            rc = -errno;
            return MAP_FAILED;
        }

        return memAddr;
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
    bool responseReceived = false;

  private:
    int xdmaFd;
    void* memAddr;
    int rc = 0;
    uint32_t pageAlignedLength;
    int32_t SourceFd;
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

template <class DMAInterface>
Response transferAll(DMAInterface* intf, int32_t file, uint32_t offset,
                     uint32_t length, uint64_t address, bool upstream,
                     ResponseHdr& responseHdr, sdeventplus::Event& event)
{
    // static CustomFDL fd(file);
    intf->setDMASourceFd(file);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    static IOPart part;
    part.length = length;
    part.offset = offset;
    part.address = address;
    auto timerCb = [=, &intf](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!intf->responseReceived)
        {
            if (intf != nullptr)
            {
                std::cerr
                    << "Failed to Complete the DMA transfer, EventLoop Timeout... \n";
                delete intf;
                intf = nullptr;
            }
        }
    };
    int xdmaFd = intf->DMASocketFd(true, true);
    auto callback = [=, &intf](IO& io, int xdmaFd, uint32_t revents) {
        if (!(revents & (EPOLLIN)))
        {
            return;
        }
        Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        io.set_fd(xdmaFd);
        while (part.length > dma::maxSize)
        {
            auto rc = intf->transferDataHost(file, part.offset, dma::maxSize,
                                             part.address, upstream);

            part.length -= dma::maxSize;
            part.offset += dma::maxSize;
            part.address += dma::maxSize;
            if (rc < 0)
            {
                encode_rw_file_memory_resp(responseHdr.instance_id,
                                           responseHdr.command, PLDM_ERROR, 0,
                                           responsePtr);
                return;
            }
        }
        auto rc = intf->transferDataHost(file, part.offset, part.length,
                                         part.address, upstream);
        if (rc < 0)
        {
            encode_rw_file_memory_resp(responseHdr.instance_id,
                                       responseHdr.command, PLDM_ERROR, 0,
                                       responsePtr);
            responseHdr.respInterface->sendPLDMRespMsg(response);
            return;
        }
        intf->responseReceived = true;
        encode_rw_file_memory_resp(responseHdr.instance_id, responseHdr.command,
                                   PLDM_SUCCESS, origLength, responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response);
        return;
    };

    try
    {
        static Timer time(event,
                          (Clock(event).now() + std::chrono::seconds{10}),
                          std::chrono::seconds{1}, std::move(timerCb));

        static IO io(event, xdmaFd, EPOLLIN, std::move(callback));
    }
    catch (const std::runtime_error& e)
    {
        Response response(sizeof(pldm_msg_hdr) + responseHdr.command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        std::cerr << "Failed to start the event loop. RC = " << e.what()
                  << "\n";
        if (intf != nullptr)
        {
            delete intf;
            intf = nullptr;
        }
        encode_rw_file_memory_resp(responseHdr.instance_id, responseHdr.command,
                                   PLDM_ERROR, 0, responsePtr);
        responseHdr.respInterface->sendPLDMRespMsg(response);
        return {};
    }
    return {};
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
            pldm::requester::Handler<pldm::requester::Request>* handler,
            pldm::Response_api::Transport* respInterface) :
        oemPlatformHandler(oemPlatformHandler),
        hostSockFd(hostSockFd), hostEid(hostEid),
        dbusImplReqester(dbusImplReqester), handler(handler),
        responseHdr({0, 0, respInterface}),
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
    ResponseHdr responseHdr;
    sdeventplus::Event event;
};

} // namespace oem_ibm
} // namespace responder
} // namespace pldm
