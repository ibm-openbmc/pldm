#pragma once

#include "oem_ibm_handler.hpp"
#include "pldmd/pldm_resp_interface.hpp"

#include <libpldm/oem/ibm/file_io.h>

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
#include <stdplus/signal.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <vector>

namespace pldm
{

namespace responder
{

using namespace sdeventplus;
using namespace sdeventplus::source;
constexpr auto clockId = sdeventplus::ClockId::RealTime;
using Timer = Time<clockId>;
using Clock = Clock<clockId>;

class FileHandler;

namespace dma
{
class DMA;
} // namespace dma

/**
 *  @class SharedAIORespData
 *         This structure data is sharing common code across File IO transfer
 *         mechanism.
 *
 */
struct SharedAIORespData
{
    /* Contain instance id to prepare response once data transfer is done.*/
    uint8_t instance_id;

    /* Contain command id to prepare response once data transfer is done.*/
    uint8_t command;

    /* Holding DMA FileHandler class object pointer to perform post operation in
     * PLDM repo once data transfer is done */
    std::shared_ptr<FileHandler> functionPtr;

    /* Contain fileType to trace log in error case */
    uint16_t fileType;

    /* Contain response pointer to  send response to remote terminus once data
     * transfer is done.*/
    pldm::response_api::AltResponse* respInterface;
};

namespace fs = std::filesystem;

/**
 *  @class FileHandler
 *
 *  Base class to handle read/write of all oem file types
 */
class FileHandler
{
  protected:
    /** @brief Method to send response to remote terminus after completion of
     * DMA operation
     *  @param[in] sharedAIORespDataobj - contain response related data
     *  @param[in] rStatus - operation status either success/fail/not suppoted.
     *  @param[in] length - length to be read/write mentioned by Remote terminus
     */
    virtual void dmaResponseToRemoteTerminus(
        const SharedAIORespData& sharedAIORespDataobj,
        const pldm_completion_codes rStatus, uint32_t length);
    /** @brief Method to send response to remote terminus after completion of
     * DMA operation
     *  @param[in] sharedAIORespDataobj - contain response related data
     *  @param[in] rStatus - operation status either success/fail/not suppoted.
     *  @param[in] length - length to be read/write mentioned by Remote terminus
     */
    virtual void dmaResponseToRemoteTerminus(
        const SharedAIORespData& sharedAIORespDataobj,
        const pldm_fileio_completion_codes rStatus, uint32_t length);
    /** @brief Method to delete all shared pointer object
     *  @param[in] sharedAIORespDataobj - contain response related data
     *  @param[in] xdmaInterface - interface to transfer data between BMC and
     * Remote terminus
     */
    virtual void
        deleteAIOobjects(const std::shared_ptr<dma::DMA>& xdmaInterface,
                         const SharedAIORespData& sharedAIORespDataobj);

  public:
    /** @brief Method to write an oem file type from remote terminus memory.
     * Individual file types need to override this method to do the file
     * specific processing
     *  @param[in] offset - offset to read/write
     *  @param[in] length - length to be read/write mentioned by remote terminus
     *  @param[in] address - DMA address
     *  @param[in] oemPlatformHandler - oem handler for PLDM platform related
     *                                  tasks
     *  @param[in] sharedAIORespDataobj - contain response related data
     *  @param[in] event - sdbusplus event object
     *  @return PLDM status code
     */
    virtual void writeFromMemory(uint32_t offset, uint32_t length,
                                 uint64_t address,
                                 oem_platform::Handler* oemPlatformHandler,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& event) = 0;

    /** @brief Method to read an oem file type into remote terminus memory.
     * Individual file types need to override this method to do the file
     * specific processing
     *  @param[in] offset - offset to read
     *  @param[in] length - length to be read mentioned by remote terminus
     *  @param[in] address - DMA address
     *  @param[in] oemPlatformHandler - oem handler for PLDM platform related
     *                                  tasks
     *  @return PLDM status code
     */
    virtual void readIntoMemory(uint32_t offset, uint32_t length,
                                uint64_t address,
                                oem_platform::Handler* oemPlatformHandler,
                                SharedAIORespData& sharedAIORespDataobj,
                                sdeventplus::Event& event) = 0;

    /** @brief Method to read an oem file type's content into the PLDM response.
     *  @param[in] offset - offset to read
     *  @param[in/out] length - length to be read
     *  @param[in] response - PLDM response
     *  @param[in] oemPlatformHandler - oem handler for PLDM platform related
     *                                  tasks
     *  @return PLDM status code
     */
    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* oemPlatformHandler) = 0;

    /** @brief Method to write an oem file by type
     *  @param[in] buffer - buffer to be written to file
     *  @param[in] offset - offset to write to
     *  @param[in/out] length - length to be written
     *  @param[in] oemPlatformHandler - oem handler for PLDM platform related
     *                                  tasks
     *  @return PLDM status code
     */
    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* oemPlatformHandler) = 0;

    virtual int fileAck(uint8_t fileStatus) = 0;

    /** @brief Method to process a new file available notification from the
     *  host. The bmc can chose to do different actions based on the file type.
     *
     *  @param[in] length - size of the file content to be transferred
     *
     *  @return PLDM status code
     */
    virtual int newFileAvailable(uint64_t length) = 0;

    /** @brief Method to read an oem file type's content into the PLDM response.
     *  @param[in] filePath - file to read from
     *  @param[in] offset - offset to read
     *  @param[in/out] length - length to be read
     *  @param[in] response - PLDM response
     *  @return PLDM status code
     */
    virtual int readFile(const std::string& filePath, uint32_t offset,
                         uint32_t& length, Response& response);

    /** @brief method to read an oem file type's content using file descriptor
     * into PLDM response
     *
     *  @param[in] fd - the file descriptor
     *  @param[in] offset - offset to read
     *  @param[in/out] length - length to be read
     *  @param[in] response - PLDM response
     *
     *  @return PLDM status code
     */
    virtual int readFileByFd(int fd, uint32_t offset, uint32_t& length,
                             Response& response);

    /** @brief Method to do the file content transfer ove DMA between host and
     *         bmc. This method is made virtual to be overridden in test case.
     * And need not be defined in other child classes
     *
     *  @param[in] path - file system path  where read/write will be done
     *  @param[in] upstream - direction of DMA transfer. "false" means a
     *                        transfer from host to BMC
     *  @param[in] offset - offset to read/write
     *  @param[in/out] length - length to be read/write mentioned by Host
     *  @param[in] address - DMA address
     *
     *  @return PLDM status code
     */
    virtual void transferFileData(const fs::path& path, bool upstream,
                                  uint32_t offset, uint32_t& length,
                                  uint64_t address,
                                  SharedAIORespData& sharedAIORespDataobj,
                                  sdeventplus::Event& event);

    virtual void transferFileData(int fd, bool upstream, uint32_t offset,
                                  uint32_t& length, uint64_t address,
                                  SharedAIORespData& sharedAIORespDataobj,
                                  sdeventplus::Event& event);

    virtual void
        transferFileDataToSocket(int fd, uint32_t& length, uint64_t address,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& event);

    /** @brief Method to do necessary operation according different
     *         file type and being call when data transfer completed.
     *
     *  @param[in] IsWriteToMemOp - type of operation to decide what operation
     *                              needs to be done after data transfer.
     *  @param[in] length    -  To do post file transfer operation based on
     * length
     */
    virtual void postDataTransferCallBack(bool IsWriteToMemOp,
                                          uint32_t length) = 0;

    /** @brief method to process a new file available metadata notification from
     *  the host
     *
     *  @param[in] length - size of the file content to be transferred
     *  @param[in] metaDataValue1 - value of meta data sent by host
     *  @param[in] metaDataValue2 - value of meta data sent by host
     *  @param[in] metaDataValue3 - value of meta data sent by host
     *  @param[in] metaDataValue4 - value of meta data sent by host
     *
     *  @return PLDM status code
     */
    virtual int newFileAvailableWithMetaData(uint64_t length,
                                             uint32_t metaDataValue1,
                                             uint32_t metaDataValue2,
                                             uint32_t metaDataValue3,
                                             uint32_t metaDataValue4) = 0;

    /** @brief Method to process a file ack with meta data notification from the
     *  host. The bmc can chose to do different actions based on the file type.
     *
     *  @param[in] fileStatus - Status of the file transfer
     *  @param[in] metaDataValue1 - value of meta data sent by host
     *  @param[in] metaDataValue2 - value of meta data sent by host
     *  @param[in] metaDataValue3 - value of meta data sent by host
     *  @param[in] metaDataValue4 - value of meta data sent by host
     *
     *  @return PLDM status code
     */
    virtual int fileAckWithMetaData(uint8_t fileStatus, uint32_t metaDataValue1,
                                    uint32_t metaDataValue2,
                                    uint32_t metaDataValue3,
                                    uint32_t metaDataValue4) = 0;

    /** @brief Constructor to create a FileHandler object
     */
    FileHandler(uint32_t fileHandle) : fileHandle(fileHandle) {}

    /** FileHandler destructor
     */
    virtual ~FileHandler() {}

  protected:
    uint32_t fileHandle; //!< file handle indicating name of file or invalid
};

/** @brief Method to create individual file handler objects based on file type
 *
 *  @param[in] fileType - type of file
 *  @param[in] fileHandle - file handle
 */

std::unique_ptr<FileHandler>
    getHandlerByType(uint16_t fileType, uint32_t fileHandle);

/** @brief Method to create shared file handler objects based on file type
 *
 *  @param[in] fileType - type of file
 *  @param[in] fileHandle - file handle
 */
std::shared_ptr<FileHandler> getSharedHandlerByType(uint16_t fileType,
                                                    uint32_t fileHandle);
} // namespace responder
} // namespace pldm
