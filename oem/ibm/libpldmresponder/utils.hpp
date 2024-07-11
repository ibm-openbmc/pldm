#pragma once

#include "libpldmresponder/oem_handler.hpp"
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pldm
{
namespace responder
{
namespace utils
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/** @struct CustomFD
 *
 *  CustomFD class is created for special purpose using RAII.
 */
struct CustomFD
{
    CustomFD(const CustomFD&) = delete;
    CustomFD& operator=(const CustomFD&) = delete;
    CustomFD(CustomFD&&) = delete;
    CustomFD& operator=(CustomFD&&) = delete;

    CustomFD(int fd) : fd(fd) {}

    ~CustomFD()
    {
        if (fd >= 0)
        {
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
 *  @return   on success returns  0
 *            on failure returns -1

 */
int writeToUnixSocket(const int sock, const char* buf,
                      const uint64_t blockSize);

/** @brief checks if given FRU is IBM specific
 *
 *  @param[in] objPath - FRU object path
 *
 *  @return bool - true if IBM specific FRU
 */
bool checkIfIBMFru(const std::string& objPath);

/** @brief finds the ports under an adapter
 *
 *  @param[in] adapterObjPath - D-Bus object path for the adapter
 *
 *  @return std::vector<std::string> - port object paths
 */
std::vector<std::string> findPortObjects(const std::string& adapterObjPath);

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

} // namespace utils

namespace oem_ibm_utils
{

class Handler : public oem_utils::Handler
{
  public:
    Handler(const pldm::utils::DBusHandler* dBusIntf) :
        oem_utils::Handler(dBusIntf), dBusIntf(dBusIntf)
    {}

    /** @brief Collecting core count data and setting to Dbus properties
     *
     *  @param[in] associations - the data of entity association
     *  @param[in] entityMaps - the mapping of entity to DBus string
     *
     */
    virtual int
        setCoreCount(const pldm::utils::EntityAssociations& associations,
                     const pldm::utils::EntityMaps entityMaps);

    virtual ~Handler() = default;

  protected:
    const pldm::utils::DBusHandler* dBusIntf;
};

} // namespace oem_ibm_utils
} // namespace responder
} // namespace pldm
