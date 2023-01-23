#pragma once

#include <string>

namespace pldm
{
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

/** @brief Clear Dump Socket Write Status
 *  This function clears all the dump socket write status to "Free" during
 *  reset reload operation or when host is coming down to off state.
 *
 *  @return   None
 */
 void clearDumpSocketWriteStatus();

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
 *  @return    void
 */
void writeToUnixSocket(const int sock, const char* buf,
                      const uint64_t blockSize);
} // namespace utils
} // namespace responder
} // namespace pldm
