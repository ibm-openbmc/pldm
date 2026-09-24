#pragma once

#include "file_io_by_type.hpp"
#include "requester/handler.hpp"

#include <libpldm/instance-id.h>

#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <memory>
#include <optional>
#include <utility>

namespace pldm
{
namespace responder
{
using DumpEntryInterface = std::string;
using FileHandle = uint32_t;
using DumpTimer = sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic>;

/** @struct DumpTransferData
 *
 *  @brief Owns the watchdog timer and destination fd for an active dump
 *  transfer (system or resource dump).
 */
struct DumpTransferData
{
    std::unique_ptr<DumpTimer> timer;
    int fd;

    DumpTransferData(std::unique_ptr<DumpTimer>&& t, int f) :
        timer(std::move(t)), fd(f)
    {}

    ~DumpTransferData()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }

    // Prevent copying
    DumpTransferData(const DumpTransferData&) = delete;
    DumpTransferData& operator=(const DumpTransferData&) = delete;
    DumpTransferData(DumpTransferData&& other) noexcept :
        timer(std::move(other.timer)), fd(other.fd)
    {
        other.fd = -1;
    }
    DumpTransferData& operator=(DumpTransferData&& other) noexcept
    {
        if (this != &other)
        {
            if (fd >= 0)
            {
                close(fd);
            }
            timer = std::move(other.timer);
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
};

/** @class DumpHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  handle the dump offload/streaming from host to the destination via bmc
 */
class DumpHandler : public FileHandler
{
  public:
    // Dump transfer timeout in minutes (system and resource dump).
    // 60 minutes to accommodate large user-initiated dumps (~15 GB);
    // value can be reduced at a later time.
    static constexpr auto dumpTimeoutMinutes = 60;

    /** @brief DumpHandler constructor
     */
    DumpHandler(
        uint32_t fileHandle, uint16_t fileType,
        pldm::InstanceIdDb* instanceIdDb = nullptr,
        pldm::requester::Handler<pldm::requester::Request>* handler = nullptr) :
        FileHandler(fileHandle), dumpType(fileType), instanceIdDb(instanceIdDb),
        handler(handler)
    {}

    virtual void writeFromMemory(
        uint32_t offset, uint32_t length, uint64_t address,
        oem_platform::Handler* /*oemPlatformHandler*/,
        SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event);

    virtual void readIntoMemory(
        uint32_t offset, uint32_t length, uint64_t address,
        oem_platform::Handler* /*oemPlatformHandler*/,
        SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event);

    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/,
                      struct fileack_status_metadata& /*metaDataObj*/);

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAck(uint8_t fileStatus);

    virtual int fileAckWithMetaData(
        uint8_t /*fileStatus*/, uint32_t metaDataValue1,
        uint32_t metaDataValue2, uint32_t /*metaDataValue3*/,
        uint32_t /*metaDataValue4*/);

    virtual int newFileAvailableWithMetaData(
        uint64_t length, uint32_t metaDataValue1, uint32_t /*metaDataValue2*/,
        uint32_t /*metaDataValue3*/, uint32_t /*metaDataValue4*/);

    std::string findDumpObjPath(uint32_t fileHandle);
    std::string getOffloadUri(uint32_t fileHandle);
    void resetOffloadUri();
    uint32_t getDumpIdPrefix(uint16_t dumpType);
    virtual int postDataTransferCallBack(bool IsWriteToMemOp,
                                         uint32_t /*length*/);

    /** @brief Send a FileAck(PLDM_SUCCESS) to the host to free a dump slot
     *  after the dump file has been received by the BMC.
     *  @param[in] dumpFileHandle - source dump file handle assigned by the host
     *  @param[in] fileType - PLDM file type (PLDM_FILE_TYPE_DUMP or
     *                        PLDM_FILE_TYPE_RESOURCE_DUMP)
     */
    void sendDumpDeletionFileAck(uint32_t dumpFileHandle, uint16_t fileType);

    virtual void postWriteAction(
        const uint16_t /*fileType*/, const uint32_t /*fileHandle*/,
        const struct fileack_status_metadata& /*metaDataObj*/) {};

    /** @brief Set event loop for timer management
     *  @param[in] event - Event loop reference
     */
    static void setEventLoop(sdeventplus::Event& event)
    {
        eventLoop = &event;
    }

    /** @brief Callback when a dump transfer timeout expires
     *  @param[in] fileHandle - File handle that timed out
     */
    static void onDumpTransferTimeout(FileHandle fileHandle);

    /** @brief Initialize a dump file transfer (system or resource dump).
     *
     *  Opens the destination file under @p filePrefix, starts the watchdog
     *  timer, and registers the transfer as the single active dump transfer.
     *
     *  @param[in] fileHandle - File handle for the dump transfer
     *  @param[in] filePrefix - Filename prefix (e.g. "sysdump_" or "resdump_")
     *  @return PLDM completion code
     */
    static int initializeDumpTransfer(FileHandle fileHandle,
                                      std::string_view filePrefix);

    /** @brief DumpHandler destructor
     */
    ~DumpHandler() {}

  private:
    static int fd;                    //!< fd to manage the dump offload to bmc
    uint16_t dumpType;                //!< type of the dump
    pldm::InstanceIdDb* instanceIdDb; //!< pldm instance ID database
    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;
    std::string
        resDumpRequestDirPath; //!< directory where the resource
                               //!< dump request parameter file is stored
    int unixFd;                //!< fd to temporarily hold the fd created.

    //!< Active dump transfer (system or resource) at most one at a time.
    //!< Empty optional means no transfer is in progress.
    static std::optional<FileHandle> activeDumpHandle;
    static std::unique_ptr<DumpTransferData>
        activeDumpTransfer; //!< Owned transfer data for the active dump
    static sdeventplus::Event* eventLoop; //!< Event loop for timer management

    enum DumpRequestStatus
    {
        Success = 0x0,
        AclFileInvalid = 0x1,
        UserChallengeInvalid = 0x2,
        PermissionDenied = 0x3,
        ResourceSelectorInvalid = 0x4,
    };
    enum DumpIdPrefix
    {
        INVALID_DUMP_ID_PREFIX = 0xFF
    };
};

} // namespace responder
} // namespace pldm
