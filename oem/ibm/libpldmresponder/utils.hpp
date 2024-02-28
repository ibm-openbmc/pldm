#pragma once

#include "../../../libpldmresponder/oem_handler.hpp"
#include "host-bmc/dbus_to_host_effecters.hpp"
#include "oem/ibm/requester/dbus_to_file_handler.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace pldm
{

/** @brief Holds the file ack with meta data status and values
 */
struct fileack_status_metadata
{
    uint8_t fileStatus;
    uint32_t fileMetaData1;
    uint32_t fileMetaData2;
    uint32_t fileMetaData3;
    uint32_t fileMetaData4;
};

namespace responder
{

enum SocketWriteStatus
{
    Completed,
    InProgress,
    Free,
    Error,
    NotReady
};
namespace utils
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/** @brief Setup UNIX socket
 *  This function creates listening socket in non-blocking mode and allows only
 *  one socket connection. returns accepted socket after accepting connection
 *  from peer.
 *
 *  @param[in] socketInterface - unix socket path
 *  @return   on success returns accepted socket fd
 *            on failure returns -1
 */
int setupUnixSocket(const std::string& socketInterface);

/** @brief Write data on UNIX socket
 *  This function writes given data to a non-blocking socket.
 *  Irrespective of block size, this function make sure of writing given data
 *  on unix socket.
 *
 *  @param[in] sock - unix socket
 *  @param[in] buf -  data buffer
 *  @param[in] blockSize - size of data to write
 *  @return   on success retruns  0
 *            on failure returns -1

 */
void writeToUnixSocket(const int sock, const char* buf,
                       const uint64_t blockSize);

/** @brief Converts a binary file to json data
 *  This function converts bson data stored in a binary file to
 *  nlohmann json data
 *
 *  @param[in] path     - binary file path to fetch the bson data
 *
 *  @return   on success returns nlohmann::json object
 */
Json convertBinFileToJson(const fs::path& path);

/** @brief Converts a json data in to a binary file
 *  This function converts the json data to a binary json(bson)
 *  format and copies it to specified destination file.
 *
 *  @param[in] jsonData - nlohmann json data
 *  @param[in] path     - destination path to store the bson data
 *
 *  @return   None
 */
void convertJsonToBinaryFile(const Json& jsonData, const fs::path& path);

/** @brief Clear License Status
 *  This function clears all the license status to "Unknown" during
 *  reset reload operation or when host is coming down to off state.
 *  During the genesis mode, it skips the license status update.
 *
 *  @return   None
 */
void clearLicenseStatus();

/** @brief Clear Dump Socket Write Status
 *  This function clears all the dump socket write status to "Free" during
 *  reset reload operation or when host is coming down to off state.
 *
 *  @return   None
 */
void clearDumpSocketWriteStatus();

/** @brief Create or update the d-bus license data
 *  This function creates or updates the d-bus license details. If the input
 *  input flag is 1, then new license data will be created and if the the input
 *  flag is 2 license status will be cleared.
 *
 *  @param[in] flag - input flag, 1 : create and 2 : clear
 *
 *  @return   on success returns PLDM_SUCCESS
 *            on failure returns -1
 */
int createOrUpdateLicenseDbusPaths(const uint8_t& flag);

/** @brief Create or update the license bjects
 *  This function creates or updates the license objects as per the data passed
 *  from host.
 *
 *  @return   on success returns PLDM_SUCCESS
 *            on failure returns -1
 */
int createOrUpdateLicenseObjs();

/** @brief checks if a pcie adapter is IBM specific
 *         cable card
 *  @param[in] objPath - FRU object path
 *
 *  @return bool - true if IBM specific card
 */
bool checkIfIBMCableCard(const std::string& objPath);

/** @brief checks whether the fru is actually present
 *  @param[in] objPath - the fru object path
 *
 *  @return bool to indicate presence or absence
 */
bool checkFruPresence(const char* objPath);

/** @brief finds the ports under an adapter
 *  @param[in] cardObjPath - D-Bus object path for the adapter
 *  @param[out] portObjects - the ports under the adapter
 */
void findPortObjects(const std::string& cardObjPath,
                     std::vector<std::string>& portObjects);

/** @brief host PCIE Topology Interface
 *  @param[in] mctp_eid - MCTP Endpoint ID
 *  @param[in] hostEffecterParser - Pointer to host effecter parser
 */
void hostPCIETopologyIntf(
    uint8_t mctp_eid,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser);

/** @brief host ChapData Interface
 *  @param[in] dbusToFilehandlerObj - ref object to raise NewFileAvailable
 * request
 */
void hostChapDataIntf(
    pldm::responder::oem_fileio::Handler* dbusToFilehandlerObj);

/** @brief Fetch D-Bus object path based on location details and the type of
 * Inventory Item
 *
 *  @param[in] locationCode - property value of LocationCode
 *  @param[in] inventoryItemType - inventory item type
 *
 *  @return std::string - the D-Bus object path
 */
std::string getObjectPathByLocationCode(const std::string& locationCode,
                                        const std::string& inventoryItemType);

std::pair<std::string, std::string>
    getSlotAndAdapter(const std::string& portLocationCode);

/** @brief method to get the BusId based on the path */
uint32_t getLinkResetInstanceNumber(std::string& path);

/** @brief method to find slot objects */
void findSlotObjects(const std::string& boardObjPath,
                     std::vector<std::string>& slotObjects);

/** @brief Split the vector according to the start and end index and return
 *  back the resultant vector
 *  @param[in] inputVec - input vector
 *  @param[in] startIdx - start index
 *  @param[in] endIdx - end index
 *  @return  the resultant split vector
 */
std::vector<char> vecSplit(const std::vector<char>& inputVec,
                           const uint32_t startIdx, const uint32_t endIdx);
} // namespace utils
} // namespace responder
} // namespace pldm
