#pragma once

#include "file_io_by_type.hpp"

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
using SysDumpTimer =
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic>;

/** @struct SysDumpTransferData
 *
 *  @brief Wrapper for systemdump that owns timer and fd
 */
struct SysDumpTransferData
{
    std::unique_ptr<SysDumpTimer> timer;
    int fd;

    SysDumpTransferData(std::unique_ptr<SysDumpTimer>&& t, int f) :
        timer(std::move(t)), fd(f)
    {}

    ~SysDumpTransferData()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }

    // Prevent copying
    SysDumpTransferData(const SysDumpTransferData&) = delete;
    SysDumpTransferData& operator=(const SysDumpTransferData&) = delete;
    SysDumpTransferData(SysDumpTransferData&& other) noexcept :
        timer(std::move(other.timer)), fd(other.fd)
    {
        other.fd = -1;
    }
    SysDumpTransferData& operator=(SysDumpTransferData&& other) noexcept
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
    // System dump transfer timeout in minutes
    // timout 60 minutes to accomadate large user initiated dumps(~15GB)
    // timeout value can be reduced at a later time
    static constexpr auto sysDumpTimeoutMinutes = 60;

    /** @brief DumpHandler constructor
     */
    DumpHandler(uint32_t fileHandle, uint16_t fileType) :
        FileHandler(fileHandle), dumpType(fileType)
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

    /** @brief Callback when dump transfer timeout expires
     *  @param[in] fileHandle - File handle that timed out
     */
    static void onDumpTransferTimeout(FileHandle fileHandle);

    /** @brief Initialize system dump transfer
     *  @param[in] fileHandle - File handle for the dump transfer
     *  @return PLDM completion code
     */
    static int initializeSystemDumpTransfer(FileHandle fileHandle);

    /** @brief DumpHandler destructor
     */
    ~DumpHandler() {}

  private:
    static int fd;             //!< fd to manage the dump offload to bmc
    uint16_t dumpType;         //!< type of the dump
    std::string
        resDumpRequestDirPath; //!< directory where the resource
                               //!< dump request parameter file is stored
    int unixFd;                //!< fd to temporarily hold the fd created.

    //!< Active system dump transfer — at most one at a time.
    //!< Empty optional means no transfer is in progress.
    static std::optional<FileHandle> sysDumpHandle;
    static std::unique_ptr<SysDumpTransferData>
        sysDumpTransfer; //!< Owned transfer data for the active system dump
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
