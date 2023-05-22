#pragma once

#include "file_io_by_type.hpp"

namespace pldm
{
namespace responder
{

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

    virtual int writeFromMemory(uint32_t offset, uint32_t length,
                                uint64_t address,
                                oem_platform::Handler* /*oemPlatformHandler*/,
                                ResponseHdr& responseHdr,
                                sdeventplus::Event& event);

    virtual int readIntoMemory(uint32_t offset, uint32_t& length,
                               uint64_t address,
                               oem_platform::Handler* oemPlatformHandler,
                               ResponseHdr& responseHdr,
                               sdeventplus::Event& event);

    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAck(uint8_t fileStatus);

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t metaDataValue1,
                                    uint32_t metaDataValue2,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/);

    virtual int newFileAvailableWithMetaData(uint64_t length,
                                             uint32_t metaDataValue1,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/);

    std::string findDumpObjPath(uint32_t fileHandle);
    std::string getOffloadUri(uint32_t fileHandle);
    virtual int postDataTransferCallBack(bool IsWriteToMemOp);
    void resetOffloadUri();
    /** @brief DumpHandler destructor
     */
    ~DumpHandler() {}

  private:
    static int fd;             //!< fd to manage the dump offload to bmc
    int grc = -1;
    uint16_t dumpType;         //!< type of the dump
    std::string
        resDumpRequestDirPath; //!< directory where the resource
                               //!< dump request parameter file is stored

    enum DumpRequestStatus
    {
        Success = 0x0,
        AcfFileInvalid = 0x1,
        PasswordInvalid = 0x2,
        PermissionDenied = 0x3,
        ResourceSelectorInvalid = 0x4,
    };
};

} // namespace responder
} // namespace pldm
