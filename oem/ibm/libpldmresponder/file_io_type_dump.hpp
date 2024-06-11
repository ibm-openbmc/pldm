#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{
using DumpEntryInterface = std::string;

/** @class DumpHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  handle the dump offload/streaming from host to the destination via bmc
 */
class DumpHandler : public FileHandler
{
  public:
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

    virtual int newFileAvailableWithMetaData(
        uint64_t length, uint32_t metaDataValue1, uint32_t /*metaDataValue2*/,
        uint32_t /*metaDataValue3*/, uint32_t /*metaDataValue4*/);

    virtual int fileAckWithMetaData(
        uint8_t /*fileStatus*/, uint32_t metaDataValue1,
        uint32_t metaDataValue2, uint32_t /*metaDataValue3*/,
        uint32_t /*metaDataValue4*/);
    
    virtual int fileAckWithMetaData(
        uint8_t /*fileStatus*/, uint32_t /*metaDataValue1*/,
        uint32_t /*metaDataValue2*/, uint32_t /*metaDataValue3*/,
        uint32_t /*metaDataValue4*/)
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }

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

    enum DumpRequestStatus
    {
        Success = 0x0,
        AcfFileInvalid = 0x1,
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
