#include "file_io_type_dump.hpp"

#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include "common/utils.hpp"
#include "utils.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <stdint.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Dump/NewDump/server.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <type_traits>

PHOSPHOR_LOG2_USING;
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
        resDumpRequestDirPath = "/var/lib/pldm/resourcedump/" +
                                std::to_string(fileHandle);
    }

    // Stores the current resource dump entry path
    std::string curResDumpEntryPath{};

    if (dumpType == PLDM_FILE_TYPE_BMC_DUMP)
    {
        curResDumpEntryPath = (std::string)bmcDumpObjPath + "/" +
                              std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_SBE_DUMP)
    {
        curResDumpEntryPath = (std::string)sbeDumpObjPath + "/" +
                              std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_HOSTBOOT_DUMP)
    {
        curResDumpEntryPath = (std::string)hostbootDumpObjPath + "/" +
                              std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_HARDWARE_DUMP)
    {
        curResDumpEntryPath = (std::string)hardwareDumpObjPath + "/" +
                              std::to_string(fileHandle);
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

    try
    {
        auto method =
            bus.new_method_call(DUMP_MANAGER_BUSNAME, DUMP_MANAGER_PATH,
                                OBJECT_MANAGER_INTERFACE, "GetManagedObjects");
        auto reply = bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
        reply.read(objects);
    }

    catch (const sdbusplus::exception_t& e)
    {
        error(
            "Failure with GetManagedObjects in findDumpObjPath call, ERROR={ERR_EXCEP}",
            "ERR_EXCEP", e.what());
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
                            info("Hit the object path match for {CUR_RES_DUMP}",
                                 "CUR_RES_DUMP", curResDumpEntryPath);
                            return curResDumpEntryPath;
                        }
                    }
                    else
                    {
                        error(
                            "Invalid SourceDumpId in curResDumpEntryPath {CUR_RES_DUMP} but continuing with next entry for a match...",
                            "CUR_RES_DUMP", curResDumpEntryPath);
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

    info("newFileAvailable for NewDump");
    auto notifyObjPath = dumpObjPath;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        // Setting the Notify path for resource dump
        notifyObjPath = resDumpObjPath;
    }

    try
    {
        auto service = pldm::utils::DBusHandler().getService(notifyObjPath,
                                                             dumpInterface);
        using namespace sdbusplus::xyz::openbmc_project::Dump::server;
        auto method = bus.new_method_call(service.c_str(), notifyObjPath,
                                          dumpInterface, "Notify");
        method.append(fileHandle, length);
        bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
    }
    catch (const sdbusplus::exception_t& e)
    {
        error(
            "failed to make a d-bus call to notify a new dump request using newFileAvailable, ERROR={ERR_EXCEP}",
            "ERR_EXCEP", e.what());
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

    info("DumpHandler::resetOffloadUri path = {PATH} fileHandle = {FILE_HNDLE}",
         "PATH", path.c_str(), "FILE_HNDL", fileHandle);

    PropertyValue offloadUriValue{""};
    DBusMapping dbusMapping{path, dumpEntry, "OffloadUri", "string"};
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                   offloadUriValue);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Failed to set the OffloadUri dbus property, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.fileAck.DumpEntryOffloadUriSetFail",
            pldm::PelSeverity::ERROR);
    }
    return;
}

std::string DumpHandler::getOffloadUri(uint32_t fileHandle)
{
    auto path = findDumpObjPath(fileHandle);
    info("DumpHandler::getOffloadUri path = {PATH} fileHandle = {FILE_HNDL}",
         "PATH", path.c_str(), "FILE_HNDL", fileHandle);
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
        info("socketInterface={SOCKET_INTF}", "SOCKET_INTF", socketInterface);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Failed to get the OffloadUri d-bus property, ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
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
            error("DumpHandler::writeFromMemory: setupUnixSocket() failed");
            std::remove(socketInterface.c_str());
            resetOffloadUri();
            return PLDM_ERROR;
        }

        DumpHandler::fd = sock;
        auto rc = transferFileDataToSocket(DumpHandler::fd, length, address);
        if (rc < 0)
        {
            error(
                "DumpHandler::writeFromMemory: transferFileDataToSocket failed");
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
        error(
            "DumpHandler::writeFromMemory: Error while writing to Unix socket");
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
        error("DumpHandler::writeFromMemory: transferFileDataToSocket failed");
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
    info(
        "Enter DumpHandler::write length = {LEN} DumpHandler::fd ={FILE_DESCRIPTION}",
        "LEN", length, "FILE_DESCRIPTION", DumpHandler::fd);

    if (socketWriteStatus == Error)
    {
        error("DumpHandler::write: Error while writing to Unix socket");
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
        error("DumpHandler::write: Error while writing to Unix socket");
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
            error("Failue in resource dump file ack");
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
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failure in setting Progress as OperationStatus.Failed in fileAck, ERROR={ERR_EXCEP}",
                    "ERR_EXCEP", e.what());
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
                catch (const sdbusplus::exception_t& e)
                {
                    error(
                        "Failed to make a d-bus call to DUMP manager to reset source dump id of {PATH}, with ERROR={ERR_EXCEP}",
                        "PATH", path.c_str(), "ERR_EXCEP", e.what());
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
                bus.call(method,
                         std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT))
                             .count());
            }
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failed to make a d-bus method to delete the dump entry {PATH}, with ERROR={ERR_EXCEP}",
                    "PATH", path.c_str(), "ERR_EXCEP", e.what());
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
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failed to set the Offloaded dbus property to true, ERROR={ERR_EXCEP}",
                    "ERR_EXCEP", e.what());
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

int DumpHandler::readIntoMemory(uint32_t offset, uint32_t length,
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
        catch (const sdbusplus::exception_t& e)
        {
            error(
                "Failed to fetch the filepath of the dump entry {FILE_HNDLE}, ERROR={ERR_EXCEP}",
                "FILE_HNDLE", lg2::hex, fileHandle, "ERR_EXCEP", e.what());
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
        catch (const sdbusplus::exception_t& e)
        {
            error(
                "Failed to fetch the filepath of the dump entry {FILE_HNDLE}, ERROR={ERR_EXCEP}",
                "FILE_HNDL", lg2::hex, fileHandle, "ERR_EXCEP", e.what());
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
        dbusMapping.objectPath = resDumpEntryObjPath +
                                 std::to_string(fileHandle);
        dbusMapping.interface = resDumpEntry;
        dbusMapping.propertyName = "DumpRequestStatus";
        dbusMapping.propertyType = "string";

        pldm::utils::PropertyValue value =
            "com.ibm.Dump.Entry.Resource.HostResponse.Success";

        info(
            "fileAckWithMetaData with token: {META_DATA_VAL1} and status: {META_DATA_VAL2}",
            "META_DATA_VAL1", metaDataValue1, "META_DATA_VAL2", metaDataValue2);
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
                error("failed to set token for resource dump,
                ERROR={ERR_EXCEP}", "ERR_EXCEP", e.what());
            return PLDM_ERROR;
            }*/
        }

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const sdbusplus::exception_t& e)
        {
            error(
                "failed to set DumpRequestStatus property for resource dump entry, ERROR={ERR_EXCEP}",
                "ERR_EXCEP", e.what());
            return PLDM_ERROR;
        }

        if (statusCode != DumpRequestStatus::Success)
        {
            error("Failue in resource dump file ack with metadata");
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
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failure in setting Progress as OperationStatus.Failed in fileAckWithMetaData, ERROR={ERR_EXCEP}",
                    "ERR_EXCEP", e.what());
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
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failed to set the Offloaded dbus property to true, ERROR={ERR_EXCEP}",
                    "ERR_EXCEP", e.what());
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

    info("newFileAvailableWithMetaData for NewDump with token :{META_DATA_VAL}",
         "META_DATA_VAL", metaDataValue1);
    auto notifyObjPath = dumpObjPath;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        // Setting the Notify path for resource dump
        notifyObjPath = resDumpObjPath;
    }

    try
    {
        auto service = pldm::utils::DBusHandler().getService(notifyObjPath,
                                                             dumpInterface);
        using namespace sdbusplus::xyz::openbmc_project::Dump::server;
        auto method = bus.new_method_call(
            service.c_str(), notifyObjPath, dumpInterface,
            "Notify"); // need to change the method to "NotifyWithToken"
                       // once dump manager changes are merged
        method.append(fileHandle, length); // need to append metaDataValue1 once
                                           // dump manager changes are merged
        bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
    }
    catch (const sdbusplus::exception_t& e)
    {
        error(
            "failed to make a d-bus call to notify a new dump request using newFileAvailableWithMetaData, ERROR={ERR_EXCEP}",
            "ERR_EXCEP", e.what());
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.newFileAvailableWithMetaData.NewDumpNotifyFail",
            pldm::PelSeverity::ERROR);
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
