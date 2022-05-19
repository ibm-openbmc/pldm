#include "file_io_type_dump.hpp"

#include "libpldm/base.h"
#include "oem/ibm/libpldm/file_io.h"

#include "common/utils.hpp"
#include "utils.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <stdint.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Dump/NewDump/server.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <type_traits>

using namespace pldm::responder::utils;
using namespace pldm::utils;

namespace pldm
{
namespace responder
{
static constexpr auto dumpEntry = "xyz.openbmc_project.Dump.Entry";
static constexpr auto dumpObjPath = "/xyz/openbmc_project/dump/system";
static constexpr auto systemDumpEntry = "xyz.openbmc_project.Dump.Entry.System";
static constexpr auto resDumpObjPath = "/xyz/openbmc_project/dump/resource";
static constexpr auto resDumpEntryObjPath =
    "/xyz/openbmc_project/dump/resource/entry/";
static constexpr auto resDumpEntry = "com.ibm.Dump.Entry.Resource";
static constexpr auto bmcDumpObjPath = "/xyz/openbmc_project/dump/bmc/entry";
static constexpr auto hostbootDumpObjPath =
    "/xyz/openbmc_project/dump/hostboot/entry";
static constexpr auto sbeDumpObjPath = "/xyz/openbmc_project/dump/sbe/entry";
static constexpr auto hardwareDumpObjPath =
    "/xyz/openbmc_project/dump/hardware/entry";

int DumpHandler::fd = -1;
namespace fs = std::filesystem;
extern SocketWriteStatus socketWriteStatus;

std::string DumpHandler::findDumpObjPath(uint32_t fileHandle)
{
    static constexpr auto DUMP_MANAGER_BUSNAME =
        "xyz.openbmc_project.Dump.Manager";
    static constexpr auto DUMP_MANAGER_PATH = "/xyz/openbmc_project/dump";
    static constexpr auto OBJECT_MANAGER_INTERFACE =
        "org.freedesktop.DBus.ObjectManager";
    auto& bus = pldm::utils::DBusHandler::getBus();

    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        resDumpRequestDirPath =
            "/var/lib/pldm/resourcedump/" + std::to_string(fileHandle);
    }

    // Stores the current resource dump entry path
    std::string curResDumpEntryPath{};

    if (dumpType == PLDM_FILE_TYPE_BMC_DUMP)
    {
        curResDumpEntryPath =
            (std::string)bmcDumpObjPath + "/" + std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_SBE_DUMP)
    {
        curResDumpEntryPath =
            (std::string)sbeDumpObjPath + "/" + std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_HOSTBOOT_DUMP)
    {
        curResDumpEntryPath =
            (std::string)hostbootDumpObjPath + "/" + std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_HARDWARE_DUMP)
    {
        curResDumpEntryPath =
            (std::string)hardwareDumpObjPath + "/" + std::to_string(fileHandle);
    }

    std::string dumpEntryIntf{};
    if ((dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP) ||
        (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS))
    {
        dumpEntryIntf = resDumpEntry;
    }
    else if (dumpType == PLDM_FILE_TYPE_DUMP)
    {
        dumpEntryIntf = systemDumpEntry;
    }
    else
    {
        return curResDumpEntryPath;
    }

    dbus::ObjectValueTree objects;
    auto method =
        bus.new_method_call(DUMP_MANAGER_BUSNAME, DUMP_MANAGER_PATH,
                            OBJECT_MANAGER_INTERFACE, "GetManagedObjects");

    try
    {
        auto reply = bus.call(method);
        reply.read(objects);
    }

    catch (const std::exception& e)
    {
        std::cerr
            << "Failure with GetManagedObjects in findDumpObjPath call, ERROR="
            << e.what() << "\n";
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.findDumpObjPath.GetManagedObjectsFail",
            pldm::PelSeverity::WARNING);
        return curResDumpEntryPath;
    }

    for (const auto& object : objects)
    {
        for (const auto& interface : object.second)
        {
            if (interface.first != dumpEntryIntf)
            {
                continue;
            }

            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "SourceDumpId")
                {
                    auto dumpIdPtr = std::get_if<uint32_t>(&propertyMap.second);
                    if (dumpIdPtr != nullptr)
                    {
                        auto dumpId = *dumpIdPtr;
                        if (fileHandle == dumpId)
                        {
                            curResDumpEntryPath = object.first.str;
                            std::cout << "Hit the object path match for"
                                      << curResDumpEntryPath << std::endl;
                            return curResDumpEntryPath;
                        }
                    }
                    else
                    {
                        std::cerr
                            << "Invalid SourceDumpId in curResDumpEntryPath "
                            << curResDumpEntryPath
                            << " but continuing with next entry for a match..."
                            << std::endl;
                    }
                }
            }
        }
    }
    return curResDumpEntryPath;
}

int DumpHandler::newFileAvailable(uint64_t length)
{
    static constexpr auto dumpInterface = "xyz.openbmc_project.Dump.NewDump";
    auto& bus = pldm::utils::DBusHandler::getBus();

    std::cout << "newFileAvailable for NewDump" << std::endl;
    auto notifyObjPath = dumpObjPath;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        // Setting the Notify path for resource dump
        notifyObjPath = resDumpObjPath;
    }

    try
    {
        auto service =
            pldm::utils::DBusHandler().getService(notifyObjPath, dumpInterface);
        using namespace sdbusplus::xyz::openbmc_project::Dump::server;
        auto method = bus.new_method_call(service.c_str(), notifyObjPath,
                                          dumpInterface, "Notify");
        method.append(fileHandle, length);
        bus.call(method);
    }
    catch (const std::exception& e)
    {
        std::cerr << "failed to make a d-bus call to notify"
                     " a new dump request using newFileAvailable, ERROR="
                  << e.what() << "\n";
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.newFileAvailable.NewDumpNotifyFail",
            pldm::PelSeverity::ERROR);
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

void DumpHandler::resetOffloadUri()
{
    auto path = findDumpObjPath(fileHandle);
    if (path.empty())
    {
        return;
    }

    std::cout << "DumpHandler::resetOffloadUri path = " << path.c_str()
              << " fileHandle = " << fileHandle << std::endl;
    PropertyValue offloadUriValue{""};
    DBusMapping dbusMapping{path, dumpEntry, "OffloadUri", "string"};
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                   offloadUriValue);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to set the OffloadUri dbus property, ERROR="
                  << e.what() << "\n";
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.fileAck.DumpEntryOffloadUriSetFail",
            pldm::PelSeverity::ERROR);
    }
    return;
}

std::string DumpHandler::getOffloadUri(uint32_t fileHandle)
{
    auto path = findDumpObjPath(fileHandle);
    std::cout << "DumpHandler::getOffloadUri path = " << path.c_str()
              << " fileHandle = " << fileHandle << std::endl;
    if (path.empty())
    {
        return {};
    }

    std::string socketInterface{};

    try
    {
        socketInterface =
            pldm::utils::DBusHandler().getDbusProperty<std::string>(
                path.c_str(), "OffloadUri", dumpEntry);
        std::cout << "socketInterface=" << socketInterface << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to get the OffloadUri d-bus property, ERROR="
                  << e.what() << "\n";
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.DumpHandler.getOffloadUriFail",
            pldm::PelSeverity::ERROR);
    }

    return socketInterface;
}

int DumpHandler::writeFromMemory(uint32_t, uint32_t length, uint64_t address,
                                 oem_platform::Handler* /*oemPlatformHandler*/)
{
    if (DumpHandler::fd == -1)
    {
        auto socketInterface = getOffloadUri(fileHandle);
        int sock = setupUnixSocket(socketInterface);
        if (sock < 0)
        {
            sock = -errno;
            close(DumpHandler::fd);
            std::cerr
                << "DumpHandler::writeFromMemory: setupUnixSocket() failed"
                << std::endl;
            std::remove(socketInterface.c_str());
            resetOffloadUri();
            return PLDM_ERROR;
        }

        DumpHandler::fd = sock;
        auto rc = transferFileDataToSocket(DumpHandler::fd, length, address);
        if (rc < 0)
        {
            std::cerr
                << "DumpHandler::writeFromMemory: transferFileDataToSocket failed"
                << std::endl;
            if (DumpHandler::fd >= 0)
            {
                close(DumpHandler::fd);
                DumpHandler::fd = -1;
            }
            std::remove(socketInterface.c_str());
            resetOffloadUri();
            return PLDM_ERROR;
        }
        return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
    }

    if (socketWriteStatus == Error)
    {
        std::cerr
            << "DumpHandler::writeFromMemory: Error while writing to Unix socket"
            << std::endl;
        if (DumpHandler::fd >= 0)
        {
            close(DumpHandler::fd);
            DumpHandler::fd = -1;
        }
        auto socketInterface = getOffloadUri(fileHandle);
        std::remove(socketInterface.c_str());
        resetOffloadUri();
        return PLDM_ERROR;
    }
    else if (socketWriteStatus == InProgress || socketWriteStatus == NotReady)
    {
        return PLDM_ERROR_NOT_READY;
    }

    auto rc = transferFileDataToSocket(DumpHandler::fd, length, address);
    if (rc < 0)
    {
        std::cerr
            << "DumpHandler::writeFromMemory: transferFileDataToSocket failed"
            << std::endl;
        if (DumpHandler::fd >= 0)
        {
            close(DumpHandler::fd);
            DumpHandler::fd = -1;
        }
        auto socketInterface = getOffloadUri(fileHandle);
        std::remove(socketInterface.c_str());
        resetOffloadUri();
        return PLDM_ERROR;
    }
    return rc < 0 ? PLDM_ERROR : PLDM_SUCCESS;
}

int DumpHandler::write(const char* buffer, uint32_t, uint32_t& length,
                       oem_platform::Handler* /*oemPlatformHandler*/)
{
    std::cout << "Enter DumpHandler::write length = " << length
              << " DumpHandler::fd = " << DumpHandler::fd << std::endl;

    if (socketWriteStatus == Error)
    {
        std::cerr << "DumpHandler::write: Error while writing to Unix socket"
                  << std::endl;
        close(fd);
        auto socketInterface = getOffloadUri(fileHandle);
        std::remove(socketInterface.c_str());
        resetOffloadUri();
        return PLDM_ERROR;
    }
    else if (socketWriteStatus == InProgress)
    {
        return PLDM_ERROR_NOT_READY;
    }

    writeToUnixSocket(DumpHandler::fd, buffer, length);
    if (socketWriteStatus == Error)
    {
        std::cerr << "DumpHandler::write: Error while writing to Unix socket"
                  << std::endl;
        close(fd);
        auto socketInterface = getOffloadUri(fileHandle);
        std::remove(socketInterface.c_str());
        resetOffloadUri();
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int DumpHandler::fileAck(uint8_t fileStatus)
{
    auto path = findDumpObjPath(fileHandle);
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        if (fileStatus != PLDM_SUCCESS)
        {
            std::cerr << "Failue in resource dump file ack" << std::endl;
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.fileAck.ResourceDumpFileAckFail",
                PelSeverity::INFORMATIONAL);

            PropertyValue value{
                "xyz.openbmc_project.Common.Progress.OperationStatus.Failed"};
            DBusMapping dbusMapping{path, "xyz.openbmc_project.Common.Progress",
                                    "Status", "string"};
            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failure in setting Progress as OperationStatus.Failed"
                       "in fileAck, ERROR="
                    << e.what() << "\n";
            }
        }

        if (fs::exists(resDumpRequestDirPath))
        {
            fs::remove_all(resDumpRequestDirPath);
        }
        return PLDM_SUCCESS;
    }

    if (!path.empty())
    {
        if (fileStatus == PLDM_ERROR_FILE_DISCARDED)
        {
            if (dumpType == PLDM_FILE_TYPE_DUMP ||
                dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
            {
                uint32_t val = 0xFFFFFFFF;
                PropertyValue value = static_cast<uint32_t>(val);
                auto dumpIntf = resDumpEntry;

                if (dumpType == PLDM_FILE_TYPE_DUMP)
                {
                    dumpIntf = systemDumpEntry;
                }

                DBusMapping dbusMapping{path.c_str(), dumpIntf, "SourceDumpId",
                                        "uint32_t"};
                try
                {
                    pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                               value);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Failed to make a d-bus call to DUMP "
                                 "manager to reset source dump id of "
                              << path.c_str() << ", with ERROR=" << e.what()
                              << "\n";
                    pldm::utils::reportError(
                        "xyz.openbmc_project.PLDM.Error.fileAck.SourceDumpIdResetFail",
                        pldm::PelSeverity::ERROR);
                    return PLDM_ERROR;
                }
            }

            auto& bus = pldm::utils::DBusHandler::getBus();
            try
            {
                auto method = bus.new_method_call(
                    "xyz.openbmc_project.Dump.Manager", path.c_str(),
                    "xyz.openbmc_project.Object.Delete", "Delete");
                bus.call(method);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failed to make a d-bus method to delete the dump entry "
                    << path.c_str() << ", with ERROR=" << e.what() << "\n";
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.fileAck.DumpEntryDeleteFail",
                    pldm::PelSeverity::ERROR);
                return PLDM_ERROR;
            }
            return PLDM_SUCCESS;
        }

        if (dumpType == PLDM_FILE_TYPE_DUMP ||
            dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
        {

            if (socketWriteStatus == InProgress)
            {
                return PLDM_ERROR_NOT_READY;
            }

            PropertyValue value{true};
            DBusMapping dbusMapping{path, dumpEntry, "Offloaded", "bool"};
            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failed to set the Offloaded dbus property to true, ERROR="
                    << e.what() << "\n";
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.fileAck.DumpEntryOffloadedSetFail",
                    pldm::PelSeverity::ERROR);
                resetOffloadUri();
                return PLDM_ERROR;
            }

            auto socketInterface = getOffloadUri(fileHandle);
            if (DumpHandler::fd >= 0)
            {
                close(DumpHandler::fd);
                DumpHandler::fd = -1;
            }
            std::remove(socketInterface.c_str());
            resetOffloadUri();
        }
        return PLDM_SUCCESS;
    }

    return PLDM_ERROR;
}

int DumpHandler::readIntoMemory(uint32_t offset, uint32_t& length,
                                uint64_t address,
                                oem_platform::Handler* /*oemPlatformHandler*/)
{
    auto path = findDumpObjPath(fileHandle);
    static constexpr auto dumpFilepathInterface =
        "xyz.openbmc_project.Common.FilePath";
    if ((dumpType == PLDM_FILE_TYPE_DUMP) ||
        (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP))
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    else if (dumpType != PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        try
        {
            auto filePath =
                pldm::utils::DBusHandler().getDbusProperty<std::string>(
                    path.c_str(), "Path", dumpFilepathInterface);
            auto rc = transferFileData(fs::path(filePath), true, offset, length,
                                       address);
            return rc;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to fetch the filepath of the dump entry"
                      << std::hex << fileHandle << ", error = " << e.what()
                      << "\n";
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.readIntoMemory.GetFilepathFail",
                pldm::PelSeverity::ERROR);
            return PLDM_ERROR;
        }
    }
    return transferFileData(resDumpRequestDirPath, true, offset, length,
                            address);
}

int DumpHandler::read(uint32_t offset, uint32_t& length, Response& response,
                      oem_platform::Handler* /*oemPlatformHandler*/)
{
    auto path = findDumpObjPath(fileHandle);
    static constexpr auto dumpFilepathInterface =
        "xyz.openbmc_project.Common.FilePath";
    if ((dumpType == PLDM_FILE_TYPE_DUMP) ||
        (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP))
    {
        return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    else if (dumpType != PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        try
        {
            auto filePath =
                pldm::utils::DBusHandler().getDbusProperty<std::string>(
                    path.c_str(), "Path", dumpFilepathInterface);
            auto rc = readFile(filePath.c_str(), offset, length, response);
            return rc;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to fetch the filepath of the dump entry"
                      << std::hex << fileHandle << ", error = " << e.what()
                      << "\n";
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.read.GetFilepathFail",
                pldm::PelSeverity::ERROR);
            return PLDM_ERROR;
        }
    }
    return readFile(resDumpRequestDirPath, offset, length, response);
}

int DumpHandler::fileAckWithMetaData(uint8_t /*fileStatus*/,
                                     uint32_t metaDataValue1,
                                     uint32_t metaDataValue2,
                                     uint32_t /*metaDataValue3*/,
                                     uint32_t /*metaDataValue4*/)
{
    auto path = findDumpObjPath(fileHandle);
    uint8_t statusCode = (uint8_t)metaDataValue2;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        DBusMapping dbusMapping;
        dbusMapping.objectPath =
            resDumpEntryObjPath + std::to_string(fileHandle);
        dbusMapping.interface = resDumpEntry;
        dbusMapping.propertyName = "DumpRequestStatus";
        dbusMapping.propertyType = "string";

        pldm::utils::PropertyValue value =
            "com.ibm.Dump.Entry.Resource.HostResponse.Success";

        std::cout << "fileAckWithMetaData with token: " << metaDataValue1
                  << " and status: " << metaDataValue2 << std::endl;
        if (statusCode == DumpRequestStatus::ResourceSelectorInvalid)
        {
            value =
                "com.ibm.Dump.Entry.Resource.HostResponse.ResourceSelectorInvalid";
        }
        else if (statusCode == DumpRequestStatus::AcfFileInvalid)
        {
            value = "com.ibm.Dump.Entry.Resource.HostResponse.AcfFileInvalid";
        }
        else if (statusCode == DumpRequestStatus::PasswordInvalid)
        {
            value = "com.ibm.Dump.Entry.Resource.HostResponse.PasswordInvalid";
        }
        else if (statusCode == DumpRequestStatus::PermissionDenied)
        {
            value = "com.ibm.Dump.Entry.Resource.HostResponse.PermissionDenied";
        }
        else if (statusCode == DumpRequestStatus::Success)
        {
            // Will be enabled after Dump Manager code for requestToken gets
            // merged
            /*DBusMapping dbusMapping;
            dbusMapping.objectPath =
                "/xyz/openbmc_project/dump/resource/entry/" +
            std::to_string(fileHandle); dbusMapping.interface =
            "com.ibm.Dump.Entry.Resource"; dbusMapping.propertyName =
            "requestToken"; dbusMapping.propertyType = "uint32_t";

            pldm::utils::PropertyValue value = metaDataValue1;

            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                           value);
            }
            catch (const std::exception& e)
            {
                std::cerr << "failed to set token for resource dump, ERROR="
                          << e.what() << "\n";
                return PLDM_ERROR;
            }*/
        }

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "failed to set DumpRequestStatus property for resource dump entry, "
                   "ERROR="
                << e.what() << "\n";
            return PLDM_ERROR;
        }

        if (statusCode != DumpRequestStatus::Success)
        {
            std::cerr << "Failue in resource dump file ack with metadata"
                      << std::endl;
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.fileAck.ResourceDumpFileAckWithMetaDataFail",
                pldm::PelSeverity::INFORMATIONAL);

            PropertyValue value{
                "xyz.openbmc_project.Common.Progress.OperationStatus.Failed"};
            DBusMapping dbusMapping{
                resDumpEntryObjPath + std::to_string(fileHandle),
                "xyz.openbmc_project.Common.Progress", "Status", "string"};
            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failure in setting Progress as OperationStatus.Failed"
                       "in fileAckWithMetaData, ERROR="
                    << e.what() << "\n";
            }
        }

        if (fs::exists(resDumpRequestDirPath))
        {
            fs::remove_all(resDumpRequestDirPath);
        }
        return PLDM_SUCCESS;
    }

    if (DumpHandler::fd >= 0 && !path.empty())
    {
        if (dumpType == PLDM_FILE_TYPE_DUMP ||
            dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
        {
            PropertyValue value{true};
            DBusMapping dbusMapping{path, dumpEntry, "Offloaded", "bool"};
            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failed to set the Offloaded dbus property to true, ERROR="
                    << e.what() << "\n";
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.fileAckWithMetaData.DumpEntryOffloadedSetFail",
                    pldm::PelSeverity::ERROR);
                return PLDM_ERROR;
            }

            close(DumpHandler::fd);
            auto socketInterface = getOffloadUri(fileHandle);
            std::remove(socketInterface.c_str());
            DumpHandler::fd = -1;
            resetOffloadUri();
        }
        return PLDM_SUCCESS;
    }

    return PLDM_ERROR;
}

int DumpHandler::newFileAvailableWithMetaData(uint64_t length,
                                              uint32_t metaDataValue1,
                                              uint32_t /*metaDataValue2*/,
                                              uint32_t /*metaDataValue3*/,
                                              uint32_t /*metaDataValue4*/)
{
    static constexpr auto dumpInterface = "xyz.openbmc_project.Dump.NewDump";
    auto& bus = pldm::utils::DBusHandler::getBus();

    std::cout << "newFileAvailableWithMetaData for NewDump with token :"
              << metaDataValue1 << std::endl;
    auto notifyObjPath = dumpObjPath;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        // Setting the Notify path for resource dump
        notifyObjPath = resDumpObjPath;
    }

    try
    {
        auto service =
            pldm::utils::DBusHandler().getService(notifyObjPath, dumpInterface);
        using namespace sdbusplus::xyz::openbmc_project::Dump::server;
        auto method = bus.new_method_call(
            service.c_str(), notifyObjPath, dumpInterface,
            "Notify"); // need to change the method to "NotifyWithToken"
                       // once dump manager changes are merged
        method.append(fileHandle, length); // need to append metaDataValue1 once
                                           // dump manager changes are merged
        bus.call(method);
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "failed to make a d-bus call to notify"
               " a new dump request using newFileAvailableWithMetaData, ERROR="
            << e.what() << "\n";
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.newFileAvailableWithMetaData.NewDumpNotifyFail",
            pldm::PelSeverity::ERROR);
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
