#pragma once

#include "common/utils.hpp"
#include "file_io_by_type.hpp"
#include "oem/ibm/requester/dbus_to_file_handler.hpp"
#include "oem_ibm_handler.hpp"
#include "pldmd/handler.hpp"
#include "requester/handler.hpp"

#include <fcntl.h>
#include <libpldm/base.h>
#include <libpldm/oem/ibm/file_io.h>
#include <libpldm/oem/ibm/host.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{
namespace dma
{
// The minimum data size of dma transfer in bytes
constexpr uint32_t minSize = 16;

constexpr size_t maxSize = DMA_MAXSIZE;
constexpr auto xdmaDev = "/dev/aspeed-xdma";

struct FileMetaData
{
    uint32_t length;
    uint32_t offset;
    uint64_t address;
};

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
    /** @brief DMA constructor is private to avoid object creation
     *  without lengh.
     */
    DMA() {}

  public:
    /** @brief DMA constructor
     *
     *  @param[in] length - using length allocating shared memory to transfer
     * data.
     */
    DMA(uint32_t length)
    {
        isTxComplete = false;
        memAddr = nullptr;
        xdmaFd = -1;
        sourceFd = -1;
        iotPtr = nullptr;
        iotPtrbc = nullptr;
        timer = nullptr;
        m_length = length;
        static const size_t pageSize = getpagesize();
        uint32_t numPages = m_length / pageSize;
        pageAlignedLength = numPages * pageSize;
        if (m_length > pageAlignedLength)
        {
            pageAlignedLength += pageSize;
        }
    }

    /** @brief DMA destructor
     */
    ~DMA()
    {
        if (iotPtr != nullptr)
        {
            iotPtrbc = iotPtr.release();
        }

        if (iotPtrbc != nullptr)
        {
            delete iotPtrbc;
            iotPtrbc = nullptr;
        }

        if (timer != nullptr)
        {
            auto time = timer.release();
            delete time;
        }

        if (xdmaFd > 0)
        {
            close(xdmaFd);
            xdmaFd = -1;
        }
        if (sourceFd > 0)
        {
            close(sourceFd);
            sourceFd = -1;
        }
    }
    /** @brief Method to fetch the shared memory file descriptor for data
     * transfer
     *
     * @param[in] isNonBlckSocket - Varible to decide socket should be blocking
     * / non blocking mode while opening
     *
     *  @return returns shared memory file descriptor
     */
    int getNewXdmaFd(bool isNonBlckSocket = true)
    {
        try
        {
            if (isNonBlckSocket)
            {
                xdmaFd = open(xdmaDev, O_RDWR | O_NONBLOCK);
            }
            else
            {
                xdmaFd = open(xdmaDev, O_RDWR);
            }
        }
        catch (...)
        {
            xdmaFd = -1;
        }
        return xdmaFd;
    }
    /** @brief Method to fetch the existing shared memory file descriptor for
     *  data transfer
     *  @return returns existing shared memory file descriptor
     */
    int getXdmaFd()
    {
        if (xdmaFd > 0)
        {
            return xdmaFd;
        }
        return -1;
    }

    /** @brief function will keep one copy of fd for exception case so it
     *  can close it.
     *  @param[in] fd - source path file descriptor
     */
    inline void setDMASourceFd(int fd)
    {
        sourceFd = fd;
    }

    /** @brief function will keep one copy of shared memory fd for exception
     *  case so it can close it.
     *
     *  @param[in] fd - xdma shared memory path file descriptor
     */
    inline void setXDMASourceFd(int fd)
    {
        xdmaFd = fd;
    }

    /** @brief function will return pagealignedlength to allocate memory for
     *  data transfer.
     */
    inline int32_t getpageAlignedLength()
    {
        return pageAlignedLength;
    }

    /** @brief function will return shared memory address
     *  from XDMA drive path
     */
    void* getXDMAsharedlocation()
    {
        if (xdmaFd < 0)
        {
            error(
                "Failed to get memory location due to invalid file descriptor during DMA.");
            return nullptr;
        }

        memAddr = mmap(nullptr, pageAlignedLength, PROT_WRITE | PROT_READ,
                       MAP_SHARED, xdmaFd, 0);
        if (MAP_FAILED == memAddr)
        {
            error(
                "Failed to map XDMA get memory location of length {LENGTH} with err num '{ERRNO}'",
                "LENGTH", pageAlignedLength, "ERRNO", errno);
        }

        return memAddr;
    }

    /** @brief Method to update file related metadata
     *
     *  @param[in] data - contain file related info like length, address,
     * offset.
     */
    inline void setFileMetaData(FileMetaData& data)
    {
        fileMetaData.address = data.address;
        fileMetaData.offset = data.offset;
        fileMetaData.length = data.length;
    }

    /** @brief Method to get file related metadata
     */
    inline FileMetaData& getFileMetaData()
    {
        return fileMetaData;
    }

    /** @brief Method to initialize IO instance for event loop
     *
     *  @param[in] ioptr -  pointer to manage eventloop
     */
    inline void insertIOInstance(std::unique_ptr<IO>&& ioptr)
    {
        iotPtr = std::move(ioptr);
    }

    /** @brief Method to delete cyclic dependecy while deleting object
     *  DMA interface and IO event loop has cyclic dependecy
     */
    void deleteIOInstance()
    {
        if (timer != nullptr)
        {
            auto time = timer.release();
            delete time;
        }
        if (iotPtr != nullptr)
        {
            iotPtrbc = iotPtr.release();
        }
    }

    /** @brief Method to set value for response received
     *
     *  @param[in] bresponse - transfer status
     */
    inline void setTransferStatus(bool bresponse)
    {
        isTxComplete = bresponse;
    }

    /** @brief Method to get to know tranfer success/fail.
     *
     *  @return returns true if transfer success else false on timeout.
     */
    inline bool getTransferStatus()
    {
        return isTxComplete;
    }

    /** @brief Method to initialize timer for each tranfer object
     *
     *  @param[in] event - sdeventplus event for IO object
     *  @param[in] callback - callback function pointer
     *
     *  @return returns true if timer creation success else false
     */
    bool initTimer(
        sdeventplus::Event& event,
        fu2::unique_function<void(Timer&, Timer::TimePoint)>&& callback);

    /** @brief API to transfer data between BMC and host using DMA
     *
     *  @param[in] path     - pathname of the file to transfer data
     *  @param[in] offset   - offset in the file
     *  @param[in] length   - length of the data to transfer
     *  @param[in] address  - DMA address on the host
     *  @param[in] upstream - indicates direction of the transfer; true
     *  indicates transfer to the host
     *
     *  @return returns 0 on success, negative errno on failure
     */
    int transferDataHost(int fd, uint32_t offset, uint32_t length,
                         uint64_t address, bool upstream);

    /** @brief API to transfer data on to unix socket from host using DMA
     *
     *  @param[in] path - pathname of the file to transfer data
     *  @param[in] length  - length of the data to transfer
     *  @param[in] address  - DMA address on the host
     *
     *  @return returns 0 on success, negative errno on failure
     */
    int transferHostDataToSocket(int fd, uint32_t length, uint64_t address);

    int openSourcefile(fs::path path, int flag)
    {
        sourceFd = open(path.c_str(), flag);
        if (sourceFd == -1)
        {
            error("File does not exist at {PATH}", "PATH", path.string());
        }
        return sourceFd;
    }

    inline int getsourceFileDesc()
    {
        return sourceFd;
    }

  private:
    /* flag to track data transfer completion */
    bool isTxComplete;

    /* Mapped DMA address to perform transfer operation */
    void* memAddr;

    /* File descriptor of DMA address */
    int xdmaFd;

    /* File descriptor of file which needs to be transfer */
    int sourceFd;

    /* Page alignment length to map DMA memory size during transfer */
    uint32_t pageAlignedLength;

    /* Pointer to create event loop obejct */
    std::unique_ptr<IO> iotPtr;

    /* Pointer to hold event loop obeject to track object memory to avoid memory
     * leak */
    IO* iotPtrbc;

    /* Pointer to create timer obejct for event loop object */
    std::unique_ptr<Timer> timer;

    /* Length to calculate page alignment length based on pagesize  */
    uint32_t m_length;

    /* Contain file related info like length, address, offset. */
    FileMetaData fileMetaData;
};

/** @brief Transfer the data between BMC and host by DMA using async manner.
 *
 *  There is a max size for each DMA operation, transferAllbyAsync API abstracts
 * this and the requested length is broken down into multiple DMA operations if
 * the length exceed max size.
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
void transferAllbyAsync(std::shared_ptr<DMAInterface> intf, int32_t fd,
                        uint32_t offset, uint32_t length, uint64_t address,
                        bool upstream, SharedAIORespData& sharedAIORespDataobj,
                        sdeventplus::Event& event)
{
    uint8_t command = sharedAIORespDataobj.command;
    uint8_t instance_id = sharedAIORespDataobj.instance_id;
    if (nullptr == intf)
    {
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                   responsePtr);
        error("Xdma interface is NULL");
        if (sharedAIORespDataobj.respInterface != nullptr)
        {
            sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
        }
        if (fd > 0)
            close(fd);
        return;
    }
    // intf->setDMASourceFd(file);
    uint32_t origLength = length;
    static auto& bus = pldm::utils::DBusHandler::getBus();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);
    std::weak_ptr<dma::DMA> wInterface = intf;

    dma::FileMetaData data;
    data.length = length;
    data.offset = offset;
    data.address = address;
    intf->setFileMetaData(data);
    auto timerCb = [=](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!intf->getTransferStatus())
        {
            error(
                "EventLoop Timeout..!! Terminating data tranfer file operation where transferred data length:{TXDATA} Remaining length:{REMAIN_DATA}",
                "TXDATA", origLength, "REMAIN_DATA", data.length);
            Response response(sizeof(pldm_msg_hdr) + command, 0);
            auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
            encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                       responsePtr);
            if (sharedAIORespDataobj.respInterface != nullptr)
            {
                sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
            }
            intf->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(intf)).reset();
        }
        return;
    };

    auto callback = [=](IO&, int, uint32_t revents) {
        if (!(revents & (EPOLLIN | EPOLLOUT)))
        {
            return;
        }
        auto weakPtr = wInterface.lock();
        dma::FileMetaData& data = weakPtr->getFileMetaData();
        int file = weakPtr->getsourceFileDesc();
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        int rc = 0;

        while (data.length > dma::maxSize)
        {
            rc = weakPtr->transferDataHost(file, data.offset, dma::maxSize,
                                           data.address, upstream);

            data.length -= dma::maxSize;
            data.offset += dma::maxSize;
            data.address += dma::maxSize;
            if (rc < 0)
            {
                encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                           responsePtr);
                error(
                    "Failed to transfer muliple chunks of data to remote terminus due to RC: '{RC}'.",
                    "RC", rc);
                if (sharedAIORespDataobj.respInterface != nullptr)
                {
                    sharedAIORespDataobj.respInterface->sendPLDMRespMsg(
                        response);
                }
                weakPtr->deleteIOInstance();
                (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
                return;
            }
        }
        rc = weakPtr->transferDataHost(file, data.offset, data.length,
                                       data.address, upstream);
        if (rc < 0)
        {
            encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                       responsePtr);
            error(
                "Failed to transfer single chunks of data to remote terminus due to RC: '{RC}'.",
                "RC", rc);
            if (sharedAIORespDataobj.respInterface != nullptr)
            {
                sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
            }
            weakPtr->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            return;
        }
        if (static_cast<int>(data.length) == rc)
        {
            weakPtr->setTransferStatus(true);
            encode_rw_file_memory_resp(instance_id, command, PLDM_SUCCESS,
                                       origLength, responsePtr);
            if (sharedAIORespDataobj.respInterface != nullptr)
            {
                sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
            }
            weakPtr->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(wInterface)).reset();
            return;
        }
    };

    try
    {
        int xdmaFd = intf->getNewXdmaFd();
        if (xdmaFd < 0)
        {
            error(
                "Failed to get the XDMA file descriptor while initialising event loop.");
            Response response(sizeof(pldm_msg_hdr) + command, 0);
            auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

            encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                       responsePtr);
            if (sharedAIORespDataobj.respInterface != nullptr)
            {
                sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
            }
            intf->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(intf)).reset();
            return;
        }
        if (intf->initTimer(event, std::move(timerCb)) == false)
        {
            error(
                "Failed to start the event timer while initialising event loop.");
            Response response(sizeof(pldm_msg_hdr) + command, 0);
            auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
            encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                       responsePtr);
            if (sharedAIORespDataobj.respInterface != nullptr)
            {
                sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
            }
            intf->deleteIOInstance();
            (static_cast<std::shared_ptr<dma::DMA>>(intf)).reset();
            return;
        }
        intf->insertIOInstance(std::move(std::make_unique<IO>(
            event, xdmaFd, EPOLLIN | EPOLLOUT, std::move(callback))));
    }
    catch (const std::runtime_error& e)
    {
        Response response(sizeof(pldm_msg_hdr) + command, 0);
        auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
        error("Failed to start the event loop with error: {ERROR} ", "ERROR",
              e);
        encode_rw_file_memory_resp(instance_id, command, PLDM_ERROR, 0,
                                   responsePtr);
        if (sharedAIORespDataobj.respInterface != nullptr)
        {
            sharedAIORespDataobj.respInterface->sendPLDMRespMsg(response);
        }
        intf->deleteIOInstance();
        (static_cast<std::shared_ptr<dma::DMA>>(intf)).reset();
    }
    return;
}

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
Response transferAll(DMAInterface* intf, uint8_t command, fs::path& path,
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
        error("File does not exist, path = {FILE_PATH}", "FILE_PATH",
              path.string());
        encode_rw_file_memory_resp(instanceId, command, PLDM_ERROR, 0,
                                   responsePtr);
        return response;
    }

    pldm::responder::utils::CustomFD fd(file);
    // To open socket in non blocking mode
    intf->getNewXdmaFd(false);

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
}

} // namespace dma

namespace oem_ibm
{
static constexpr auto dumpObjPath = "/xyz/openbmc_project/dump/system/entry/";
static constexpr auto resDumpEntry = "com.ibm.Dump.Entry.Resource";
static constexpr auto sysDumpEntry = "xyz.openbmc_project.Dump.Entry.System";

static constexpr auto certObjPath = "/xyz/openbmc_project/certs/ca/";
static constexpr auto certAuthority =
    "xyz.openbmc_project.PLDM.Provider.Certs.Authority.CSR";

static constexpr auto codLicObjPath = "/com/ibm/license";
static constexpr auto codLicInterface = "com.ibm.License.LicenseManager";
class Handler : public CmdHandler
{
  public:
    Handler(oem_platform::Handler* oemPlatformHandler, int hostSockFd,
            uint8_t hostEid, pldm::InstanceIdDb* instanceIdDb,
            pldm::requester::Handler<pldm::requester::Request>* handler,
            pldm::response_api::AltResponse* respInterface,
            sdeventplus::Event& event) :
        oemPlatformHandler(oemPlatformHandler), instanceIdDb(instanceIdDb),
        handler(handler),
        sharedAIORespDataobj({0, 0, nullptr, 0, respInterface}), event(event)
    {
        handlers.emplace(
            PLDM_READ_FILE_INTO_MEMORY,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->readFileIntoMemory(request, payloadLength);
            });
        handlers.emplace(
            PLDM_WRITE_FILE_FROM_MEMORY,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->writeFileFromMemory(request, payloadLength);
            });
        handlers.emplace(
            PLDM_WRITE_FILE_BY_TYPE_FROM_MEMORY,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->writeFileByTypeFromMemory(request, payloadLength);
            });
        handlers.emplace(
            PLDM_READ_FILE_BY_TYPE_INTO_MEMORY,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->readFileByTypeIntoMemory(request, payloadLength);
            });
        handlers.emplace(
            PLDM_READ_FILE_BY_TYPE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->readFileByType(request, payloadLength);
            });
        handlers.emplace(
            PLDM_WRITE_FILE_BY_TYPE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->writeFileByType(request, payloadLength);
            });
        handlers.emplace(
            PLDM_GET_FILE_TABLE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->getFileTable(request, payloadLength);
            });
        handlers.emplace(
            PLDM_READ_FILE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->readFile(request, payloadLength);
            });
        handlers.emplace(
            PLDM_WRITE_FILE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->writeFile(request, payloadLength);
            });
        handlers.emplace(
            PLDM_FILE_ACK,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->fileAck(request, payloadLength);
            });
        handlers.emplace(
            PLDM_HOST_GET_ALERT_STATUS,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->getAlertStatus(request, payloadLength);
            });
        handlers.emplace(
            PLDM_NEW_FILE_AVAILABLE,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->newFileAvailable(request, payloadLength);
            });
        handlers.emplace(
            PLDM_NEW_FILE_AVAILABLE_WITH_META_DATA,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->newFileAvailableWithMetaData(request,
                                                          payloadLength);
            });
        handlers.emplace(
            PLDM_FILE_ACK_WITH_META_DATA,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->fileAckWithMetaData(request, payloadLength);
            });
        handlers.emplace(
            PLDM_NEW_FILE_AVAILABLE_WITH_META_DATA,
            [this](pldm_tid_t, const pldm_msg* request, size_t payloadLength) {
                return this->newFileAvailableWithMetaData(request,
                                                          payloadLength);
            });

        resDumpMatcher = std::make_unique<sdbusplus::bus::match_t>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, dumpObjPath),
            [this, hostSockFd, hostEid, instanceIdDb,
             handler](sdbusplus::message_t& msg) {
                std::map<
                    std::string,
                    std::map<std::string, std::variant<std::string, uint32_t>>>
                    interfaces;
                sdbusplus::message::object_path path;
                msg.read(path, interfaces);
                std::string vspstring;
                std::string userchallenge;

                for (const auto& interface : interfaces)
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
                            else if (property.first == "UserChallenge")
                            {
                                userchallenge =
                                    std::get<std::string>(property.second);
                            }
                        }
                        dbusToFileHandlers
                            .emplace_back(
                                std::make_unique<pldm::requester::oem_ibm::
                                                     DbusToFileHandler>(
                                    hostSockFd, hostEid, instanceIdDb, path,
                                    handler))
                            ->processNewResourceDump(vspstring, userchallenge);
                        break;
                    }
                    if (interface.first == sysDumpEntry)
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "SystemImpact")
                            {
                                if (std::get<std::string>(property.second) ==
                                    "xyz.openbmc_project.Dump.Entry.System.SystemImpact.NonDisruptive")
                                {
                                    vspstring = "system";
                                }
                                else if (
                                    std::get<std::string>(property.second) ==
                                    "xyz.openbmc_project.Dump.Entry.System.SystemImpact.Disruptive")
                                {
                                    return; // it is a disruptive system dump,
                                            // ignore
                                }
                            }
                            else if (property.first == "UserChallenge")
                            {
                                userchallenge =
                                    std::get<std::string>(property.second);
                            }
                        }
                        dbusToFileHandlers
                            .emplace_back(
                                std::make_unique<pldm::requester::oem_ibm::
                                                     DbusToFileHandler>(
                                    hostSockFd, hostEid, instanceIdDb, path,
                                    handler))
                            ->processNewResourceDump(vspstring, userchallenge);
                        break;
                    }
                }
            });
        vmiCertMatcher = std::make_unique<sdbusplus::bus::match_t>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(0, certObjPath),
            [this, hostSockFd, hostEid, instanceIdDb,
             handler](sdbusplus::message_t& msg) {
                std::map<
                    std::string,
                    std::map<std::string, std::variant<std::string, uint32_t>>>
                    interfaces;
                sdbusplus::message::object_path path;
                msg.read(path, interfaces);
                std::string csr;

                for (const auto& interface : interfaces)
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
                                        hostSockFd, hostEid, instanceIdDb, path,
                                        handler))
                                    ->newCsrFileAvailable(csr, fileHandle);
                                break;
                            }
                        }
                        break;
                    }
                }
            });
        codLicenseSubs = std::make_unique<sdbusplus::bus::match_t>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::propertiesChanged(codLicObjPath,
                                                            codLicInterface),
            [this, hostSockFd, hostEid, instanceIdDb,
             handler](sdbusplus::message_t& msg) {
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
                                    hostSockFd, hostEid, instanceIdDb, path,
                                    handler))
                            ->newLicFileAvailable(licenseStr);
                        break;
                    }
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

    /** @brief Handler for newFileAvailableWithMetaData command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response messsage
     */
    Response newFileAvailableWithMetaData(const pldm_msg* request,
                                          size_t payloadLength);

    /** @brief Handler for fileAckWithMetaData command
     *
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *
     *  @return PLDM response message
     */
    Response fileAckWithMetaData(const pldm_msg* request, size_t payloadLength);

    /** @brief Handler for read/write file by type into memory command
     *
     *  @param[in] request - PLDM read/write command
     *  @param[in] request - PLDM request msg
     *  @param[in] payloadLength - length of the message payload
     *  @param[in] oemPlatformHandler - oem platform handler
     *  @param[in] instanceIdDb - pldm instance DB requester
     *
     *  @return PLDM response messsage
     */
    Response rwFileByTypeIntoMemory(uint8_t cmd, const pldm_msg* request,
                                    size_t payloadLength,
                                    oem_platform::Handler* oemPlatformHandler,
                                    SharedAIORespData& sharedAIORespDataobj,
                                    pldm::InstanceIdDb* instanceIdDb);

    /** @brief Execute the post write call back actions after sending back the
     *  write command response to the host
     *  @param[in] fileType - type of the file
     *  @param[in] fileHandle - file handle
     *  @param[in] metaDataObj - file ack meta data status and values
     */
    void postWriteCallBack(const uint16_t& fileType, const uint32_t& fileHandle,
                           const struct fileack_status_metadata& metaDataObj);
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
    pldm::InstanceIdDb* instanceIdDb;
    using DBusInterfaceAdded = std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::variant<std::string>>>>>;
    std::unique_ptr<pldm::requester::oem_ibm::DbusToFileHandler>
        dbusToFileHandler; //!< pointer to send request to Host
    std::unique_ptr<sdbusplus::bus::match_t>
        resDumpMatcher;    //!< Pointer to capture the interface added signal
                           //!< for new resource dump
    std::unique_ptr<sdbusplus::bus::match_t>
        vmiCertMatcher;    //!< Pointer to capture the interface added signal
                           //!< for new csr string
    std::unique_ptr<sdbusplus::bus::match_t>
        codLicenseSubs;    //!< Pointer to capture the property changed signal
                           //!< for new license string
    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;
    std::vector<std::unique_ptr<pldm::requester::oem_ibm::DbusToFileHandler>>
        dbusToFileHandlers;
    SharedAIORespData sharedAIORespDataobj;

    /** @brief sdeventplus event */
    sdeventplus::Event& event;
    /** @brief sdeventplus defer event source */
    std::unique_ptr<sdeventplus::source::Defer> smsEvent;
};

} // namespace oem_ibm
} // namespace responder
} // namespace pldm
