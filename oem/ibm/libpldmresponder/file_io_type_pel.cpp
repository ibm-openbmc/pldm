#include "file_io_type_pel.hpp"

#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include "common/utils.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <stdint.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <org/open_power/Logging/PEL/server.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{
using namespace sdbusplus::xyz::openbmc_project::Logging::server;
using namespace sdbusplus::org::open_power::Logging::server;

namespace detail
{
/**
 * @brief Finds the Entry::Level value for the severity of the PEL
 *        passed in.
 *
 * The severity byte is at offset 10 in the User Header section,
 * which is always after the 48 byte Private Header section.
 *
 * @param[in] pelFileName - The file containing the PEL
 *
 * @return Entry::Level - The severity value for the Entry
 */
Entry::Level getEntryLevelFromPEL(const std::string& pelFileName)
{
    const std::map<uint8_t, Entry::Level> severityMap{
        {0x00, Entry::Level::Informational}, // Informational event
        {0x10, Entry::Level::Warning},       // Recoverable error
        {0x20, Entry::Level::Warning},       // Predictive error
        {0x40, Entry::Level::Error},         // Unrecoverable error
        {0x50, Entry::Level::Error},         // Critical error
        {0x60, Entry::Level::Error},         // Error from a diagnostic test
        {0x70, Entry::Level::Warning}        // Recoverable symptom
    };

    const size_t severityOffset = 0x3A;

    size_t size = 0;
    if (fs::exists(pelFileName))
    {
        size = fs::file_size(pelFileName);
    }

    if (size > severityOffset)
    {
        std::ifstream pel{pelFileName};
        if (pel.good())
        {
            pel.seekg(severityOffset);

            uint8_t sev;
            pel.read(reinterpret_cast<char*>(&sev), 1);

            // Get the type
            sev = sev & 0xF0;

            auto entry = severityMap.find(sev);
            if (entry != severityMap.end())
            {
                return entry->second;
            }
        }
        else
        {
            error("Unable to open PEL file {PEL_FILE_NAME}", "PEL_FILE_NAME",
                  pelFileName);
        }
    }

    return Entry::Level::Error;
}
} // namespace detail

int PelHandler::readIntoMemory(uint32_t offset, uint32_t length,
                               uint64_t address,
                               oem_platform::Handler* /*oemPlatformHandler*/)
{
    static constexpr auto logObjPath = "/xyz/openbmc_project/logging";
    static constexpr auto logInterface = "org.open_power.Logging.PEL";

    auto& bus = pldm::utils::DBusHandler::getBus();

    try
    {
        auto service = pldm::utils::DBusHandler().getService(logObjPath,
                                                             logInterface);
        auto method = bus.new_method_call(service.c_str(), logObjPath,
                                          logInterface, "GetPEL");
        method.append(fileHandle);
        auto reply = bus.call(method, dbusTimeout);
        sdbusplus::message::unix_fd fd{};
        reply.read(fd);
        auto rc = transferFileData(fd, true, offset, length, address);
        return rc;
    }
    catch (const std::exception& e)
    {
        error(
            "GetPEL D-Bus call failed, PEL id = 0x{FILE_HNDL}, error ={ERR_EXCEP}",
            "FILE_HNDL", lg2::hex, fileHandle, "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int PelHandler::read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/)
{
    static constexpr auto logObjPath = "/xyz/openbmc_project/logging";
    static constexpr auto logInterface = "org.open_power.Logging.PEL";
    auto& bus = pldm::utils::DBusHandler::getBus();

    try
    {
        auto service = pldm::utils::DBusHandler().getService(logObjPath,
                                                             logInterface);
        auto method = bus.new_method_call(service.c_str(), logObjPath,
                                          logInterface, "GetPEL");
        method.append(fileHandle);
        auto reply = bus.call(method, dbusTimeout);
        sdbusplus::message::unix_fd fd{};
        reply.read(fd);

        off_t fileSize = lseek(fd, 0, SEEK_END);
        if (fileSize == -1)
        {
            error("file seek failed");
            return PLDM_ERROR;
        }
        if (offset >= fileSize)
        {
            error(
                "PelHandler::read: Offset exceeds file size, OFFSET={OFFSET} FILE_SIZE={FILE_SIZE} FILE_HANDLE={FILE_HANDLE}",
                "OFFSET", offset, "FILE_SIZE", fileSize, "FILE_HANDLE",
                fileHandle);
            return PLDM_DATA_OUT_OF_RANGE;
        }
        if (offset + length > fileSize)
        {
            length = fileSize - offset;
        }
        auto rc = lseek(fd, offset, SEEK_SET);
        if (rc == -1)
        {
            error("file seek failed");
            return PLDM_ERROR;
        }
        size_t currSize = response.size();
        response.resize(currSize + length);
        auto filePos = reinterpret_cast<char*>(response.data());
        filePos += currSize;
        rc = ::read(fd, filePos, length);
        if (rc == -1)
        {
            error("file read failed");
            return PLDM_ERROR;
        }
        if (rc != length)
        {
            error(
                "mismatch between number of characters to read and the length read, LENGTH={LEN} COUNT={COUNT}",
                "LEN", length, "COUNT", rc);
            return PLDM_ERROR;
        }
    }
    catch (const std::exception& e)
    {
        error(
            "GetPEL D-Bus call failed on PEL ID 0x{FILE_HNDL}, error ={ERR_EXCEP}",
            "FILE_HNDL", lg2::hex, fileHandle, "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

int PelHandler::writeFromMemory(uint32_t offset, uint32_t length,
                                uint64_t address,
                                oem_platform::Handler* /*oemPlatformHandler*/)
{
    char tmpFile[] = "/tmp/pel.XXXXXX";
    int fd = mkstemp(tmpFile);
    if (fd == -1)
    {
        error("failed to create a temporary pel, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", errno);
        return PLDM_ERROR;
    }
    close(fd);
    fs::path path(tmpFile);

    auto rc = transferFileData(path, false, offset, length, address);
    if (rc == PLDM_SUCCESS)
    {
        rc = storePel(path.string());
    }
    return rc;
}

int PelHandler::fileAck(uint8_t fileStatus)
{
    static constexpr auto logObjPath = "/xyz/openbmc_project/logging";
    static constexpr auto logInterface = "org.open_power.Logging.PEL";
    static std::string service;
    auto& bus = pldm::utils::DBusHandler::getBus();

    if (service.empty())
    {
        try
        {
            service = pldm::utils::DBusHandler().getService(logObjPath,
                                                            logInterface);
        }
        catch (const sdbusplus::exception_t& e)
        {
            error("Mapper call failed when trying to find logging service "
                  "to ack PEL ID {FILE_HANDLE} error = {ERR_EXCEP}",
                  "FILE_HANDLE", lg2::hex, fileHandle, "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }

    if (fileStatus == PLDM_SUCCESS)
    {
        try
        {
            auto method = bus.new_method_call(service.c_str(), logObjPath,
                                              logInterface, "HostAck");
            method.append(fileHandle);
            bus.call_noreply(method, dbusTimeout);
        }
        catch (const std::exception& e)
        {
            error(
                "HostAck D-Bus call failed on PEL ID {FILE_HANDLE}, error = {ERR_EXCEP}",
                "FILE_HANDLE", lg2::hex, fileHandle, "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else
    {
        PEL::RejectionReason reason{};
        if (fileStatus == PLDM_FULL_FILE_DISCARDED)
        {
            reason = PEL::RejectionReason::HostFull;
        }
        else if (fileStatus == PLDM_ERROR_FILE_DISCARDED)
        {
            reason = PEL::RejectionReason::BadPEL;
        }
        else
        {
            error(
                "Invalid file status {STATUS} in PEL file ack response for PEL {FILE_HANDLE}",
                "STATUS", lg2::hex, fileStatus, "FILE_HANDLE", lg2::hex,
                fileHandle);
            return PLDM_ERROR;
        }

        try
        {
            auto method = bus.new_method_call(service.c_str(), logObjPath,
                                              logInterface, "HostReject");
            method.append(fileHandle, reason);
            bus.call_noreply(method, dbusTimeout);
        }
        catch (const std::exception& e)
        {
            error("HostReject D-Bus call failed on PEL ID {FILE_HANDLE}, "
                  "error = {ERR_EXCEP}, status = {STATUS}",
                  "FILE_HANDLE", lg2::hex, fileHandle, "ERR_EXCEP", e, "STATUS",
                  lg2::hex, fileStatus);
            return PLDM_ERROR;
        }
    }

    return PLDM_SUCCESS;
}

int PelHandler::storePel(std::string&& pelFileName)
{
    static constexpr auto logObjPath = "/xyz/openbmc_project/logging";
    static constexpr auto logInterface = "xyz.openbmc_project.Logging.Create";

    auto& bus = pldm::utils::DBusHandler::getBus();

    try
    {
        auto service = pldm::utils::DBusHandler().getService(logObjPath,
                                                             logInterface);
        using namespace sdbusplus::xyz::openbmc_project::Logging::server;
        std::map<std::string, std::string> addlData{};
        auto severity =
            sdbusplus::xyz::openbmc_project::Logging::server::convertForMessage(
                detail::getEntryLevelFromPEL(pelFileName));
        addlData.emplace("RAWPEL", std::move(pelFileName));

        auto method = bus.new_method_call(service.c_str(), logObjPath,
                                          logInterface, "Create");
        method.append("xyz.openbmc_project.Host.Error.Event", severity,
                      addlData);

        auto callback = [pelFileName](sdbusplus::message_t&& msg) {
            if (msg.is_method_error())
            {
                error(
                    "Async DBus call failed for PEL file name - '{FILE}', ERROR - {ERROR}",
                    "FILE", pelFileName, "ERROR", msg.get_error()->message);
            }
        };

        [[maybe_unused]] auto slot = method.call_async(callback);
    }
    catch (const std::exception& e)
    {
        error("failed to make a d-bus call to PEL daemon, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int PelHandler::write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/,
                      struct fileack_status_metadata& /*metaDataObj*/)
{
    int rc = PLDM_SUCCESS;

    if (offset > 0)
    {
        error("Offset is non zero");
        return PLDM_ERROR;
    }

    char tmpFile[] = "/tmp/pel.XXXXXX";
    auto fd = mkstemp(tmpFile);
    if (fd == -1)
    {
        error("failed to create a temporary pel, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", errno);
        return PLDM_ERROR;
    }

    size_t written = 0;
    do
    {
        if ((rc = ::write(fd, buffer, length - written)) == -1)
        {
            break;
        }
        written += rc;
        buffer += rc;
    } while (rc && written < length);
    close(fd);

    if (rc == -1)
    {
        error(
            "file write failed, ERROR={ERR_EXCEP}, LENGTH={LEN}, OFFSET={OFFSET}",
            "ERR_EXCEP", errno, "LEN", length, "ERR_EXCEP", offset);
        fs::remove(tmpFile);
        return PLDM_ERROR;
    }

    if (written == length)
    {
        fs::path path(tmpFile);
        rc = storePel(path.string());
        if (rc != PLDM_SUCCESS)
        {
            error("save PEL failed, ERROR = {RC} tmpFile = {TMP_FILE}", "KEY0",
                  rc, "TMP_FILE", tmpFile);
        }
    }

    return rc;
}

} // namespace responder
} // namespace pldm
