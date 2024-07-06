#include "file_io_type_dump.hpp"

#include "com/ibm/Dump/Notify/server.hpp"
#include "common/utils.hpp"
#include "utils.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <libpldm/base.h>
#include <libpldm/oem/ibm/file_io.h>
#include <stdint.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>

#include <exception>
#include <filesystem>
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
static constexpr auto resDumpEntry = "com.ibm.Dump.Entry.Resource";
static constexpr auto dumpEntryObjPath =
    "/xyz/openbmc_project/dump/system/entry/";
static constexpr auto bmcDumpObjPath = "/xyz/openbmc_project/dump/bmc/entry";

// Resource dump file path to be deleted once hyperviosr validates the input
// parameters. Need to re-look in to this name when we support multiple
// resource dumps.
static constexpr auto resDumpDirPath = "/var/lib/pldm/resourcedump/1";

int DumpHandler::fd = -1;
namespace fs = std::filesystem;

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

    std::string curDumpEntryPath{};

    if (dumpType == PLDM_FILE_TYPE_BMC_DUMP)
    {
        curDumpEntryPath = (std::string)bmcDumpObjPath + "/" +
                           std::to_string(fileHandle);
    }
    else if (dumpType == PLDM_FILE_TYPE_SBE_DUMP ||
             dumpType == PLDM_FILE_TYPE_HOSTBOOT_DUMP ||
             dumpType == PLDM_FILE_TYPE_HARDWARE_DUMP)
    {
        curDumpEntryPath = (std::string)dumpObjPath + "/" +
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
        return curDumpEntryPath;
    }

    dbus::ObjectValueTree objects;

    try
    {
        auto method =
            bus.new_method_call(DUMP_MANAGER_BUSNAME, DUMP_MANAGER_PATH,
                                OBJECT_MANAGER_INTERFACE, "GetManagedObjects");
        auto reply = bus.call(method, dbusTimeout);
        reply.read(objects);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error(
            "Failure with GetManagedObjects in findDumpObjPath call '{PATH}' and interface '{INTERFACE}', error - {ERROR}",
            "PATH", DUMP_MANAGER_PATH, "INTERFACE", dumpEntryIntf, "ERROR", e);
        return curDumpEntryPath;
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
                            curDumpEntryPath = object.first.str;
                            info("Hit the object path match for {CUR_RES_DUMP}",
                                 "CUR_RES_DUMP", curDumpEntryPath);
                            return curDumpEntryPath;
                        }
                    }
                    else
                    {
                        error(
                            "Invalid SourceDumpId in curDumpEntryPath '{CUR_RES_DUMP}' but continuing with next entry for a match...",
                            "CUR_RES_DUMP", curDumpEntryPath);
                    }
                }
            }
        }
    }
    return curDumpEntryPath;
}

int DumpHandler::newFileAvailable(uint64_t length)
{
    static constexpr auto dumpInterface = "com.ibm.Dump.Notify";
    auto& bus = pldm::utils::DBusHandler::getBus();

    auto notifyObjPath = dumpObjPath;
    auto notifyDumpType =
        sdbusplus::common::com::ibm::dump::Notify::DumpType::System;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        // Setting the dump type for resource dump
        notifyDumpType =
            sdbusplus::common::com::ibm::dump::Notify::DumpType::Resource;
    }

    try
    {
        auto service = pldm::utils::DBusHandler().getService(notifyObjPath,
                                                             dumpInterface);
        auto method = bus.new_method_call(service.c_str(), notifyObjPath,
                                          dumpInterface, "NotifyDump");
        method.append(fileHandle, length, notifyDumpType, 0);
        bus.call_noreply(method, dbusTimeout);
    }
    catch (const std::exception& e)
    {
        error(
            "Error '{ERROR}' found for new file available while notifying new dump to dump manager with object path {PATH} and interface {INTERFACE}",
            "ERROR", e, "PATH", notifyObjPath, "INTERFACE", dumpInterface);
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
         "PATH", path.c_str(), "FILE_HNDLE", fileHandle);

    PropertyValue offloadUriValue{""};
    DBusMapping dbusMapping{path, dumpEntry, "OffloadUri", "string"};
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping,
                                                   offloadUriValue);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("Failed to set the OffloadUri dbus property,error - '{ERROR}'",
              "ERROR", e);
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.fileAck.DumpEntryOffloadUriSetFail");
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
        info("Offload URI socketInterface={SOCKET_INTF}", "SOCKET_INTF",
             socketInterface);
    }
    catch (const std::exception& e)
    {
        error(
            "Error '{ERROR}' found while fetching the dump offload URI with object path '{PATH}' and interface '{INTERFACE}'",
            "ERROR", e, "PATH", path, "INTERFACE", socketInterface);
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.DumpHandler.getOffloadUriFail");
    }

    return socketInterface;
}

void DumpHandler::postDataTransferCallBack(bool IsWriteToMemOp,
                                           uint32_t /*length*/)
{
    /// execute when DMA transfer failed.
    if (IsWriteToMemOp)
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
        return;
    }
}

void DumpHandler::writeFromMemory(uint32_t, uint32_t length, uint64_t address,
                                  oem_platform::Handler* /*oemPlatformHandler*/,
                                  SharedAIORespData& sharedAIORespDataobj,
                                  sdeventplus::Event& event)
{
    if (DumpHandler::fd == -1)
    {
        auto socketInterface = getOffloadUri(fileHandle);
        int sock = setupUnixSocket(socketInterface);
        if (sock < 0)
        {
            close(DumpHandler::fd);
            error(
                "Failed to setup Unix socket while write from memory for interface '{INTERFACE}', response code '{SOCKET_RC}'",
                "INTERFACE", socketInterface, "SOCKET_RC", sock);
            std::remove(socketInterface.c_str());
            resetOffloadUri();

            FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                     PLDM_ERROR, 0);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);

            return;
        }

        DumpHandler::fd = sock;
    }

    transferFileDataToSocket(DumpHandler::fd, length, address,
                             sharedAIORespDataobj, event);
}

int DumpHandler::write(const char* buffer, uint32_t, uint32_t& length,
                       oem_platform::Handler* /*oemPlatformHandler*/)
{
    int rc = writeToUnixSocket(DumpHandler::fd, buffer, length);
    if (rc < 0)
    {
        rc = -errno;
        close(DumpHandler::fd);
        auto socketInterface = getOffloadUri(fileHandle);
        std::remove(socketInterface.c_str());
        error(
            "Failed to do dump write to Unix socket for interface '{INTERFACE}', response code '{RC}'",
            "INTERFACE", socketInterface, "RC", rc);
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
            error("Failure in resource dump file ack");

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
                error(
                    "Error '{ERROR}' found for file ack while setting the dump progress status as 'Failed' with object path '{PATH}' and interface 'xyz.openbmc_project.Common.Progress'",
                    "ERROR", e, "PATH", path);
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
                    error(
                        "Failed to make a D-bus call to DUMP manager for reseting source dump file '{PATH}' on interface '{INTERFACE}', error - {ERROR}",
                        "PATH", path, "INTERFACE", dumpIntf, "ERROR", e);
                    pldm::utils::reportError(
                        "xyz.openbmc_project.bmc.PLDM.fileAck.SourceDumpIdResetFail");
                    return PLDM_ERROR;
                }
            }
            auto& bus = pldm::utils::DBusHandler::getBus();
            try
            {
                auto method = bus.new_method_call(
                    "xyz.openbmc_project.Dump.Manager", path.c_str(),
                    "xyz.openbmc_project.Object.Delete", "Delete");
                bus.call(method, dbusTimeout);
            }
            catch (const std::exception& e)
            {
                error(
                    "Failed to make a D-bus call to DUMP manager for delete dump file '{PATH}', error - {ERROR}",
                    "PATH", path, "ERROR", e);
                pldm::utils::reportError(
                    "xyz.openbmc_project.bmc.PLDM.fileAck.DumpEntryDeleteFail");
                return PLDM_ERROR;
            }
            return PLDM_SUCCESS;
        }

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
                error(
                    "Failed to make a D-bus call to DUMP manager to set the dump offloaded property 'true' for dump file '{PATH}', error - {ERROR}",
                    "PATH", path, "ERROR", e);
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

void DumpHandler::readIntoMemory(uint32_t offset, uint32_t length,
                                 uint64_t address,
                                 oem_platform::Handler* /*oemPlatformHandler*/,
                                 SharedAIORespData& sharedAIORespDataobj,
                                 sdeventplus::Event& event)
{
    auto path = findDumpObjPath(fileHandle);
    static constexpr auto dumpFilepathInterface =
        "xyz.openbmc_project.Common.FilePath";
    if ((dumpType == PLDM_FILE_TYPE_DUMP) ||
        (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP))
    {
        FileHandler::dmaResponseToRemoteTerminus(
            sharedAIORespDataobj, PLDM_ERROR_UNSUPPORTED_PLDM_CMD, length);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        return;
    }
    else if (dumpType != PLDM_FILE_TYPE_RESOURCE_DUMP_PARMS)
    {
        try
        {
            auto filePath =
                pldm::utils::DBusHandler().getDbusProperty<std::string>(
                    path.c_str(), "Path", dumpFilepathInterface);
            transferFileData(fs::path(filePath), true, offset, length, address,
                             sharedAIORespDataobj, event);
            return;
        }
        catch (const sdbusplus::exception_t& e)
        {
            error(
                "Failed to fetch the filepath of the dump entry '{FILE_HNDLE}', error - {ERROR}",
                "FILE_HNDLE", lg2::hex, fileHandle, "ERROR", e);
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.readIntoMemory.GetFilepathFail");
            FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                     PLDM_ERROR, 0);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
            return;
        }
    }
    transferFileData(resDumpRequestDirPath, true, offset, length, address,
                     sharedAIORespDataobj, event);
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
                "Failed to fetch the filepath of the dump entry '{FILE_HNDLE}', error - {ERROR}",
                "FILE_HNDL", lg2::hex, fileHandle, "ERROR", e);
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.read.GetFilepathFail");
            return PLDM_ERROR;
        }
    }
    return readFile(resDumpRequestDirPath, offset, length, response);
}

int DumpHandler::newFileAvailableWithMetaData(uint64_t length,
                                              uint32_t metaDataValue1,
                                              uint32_t /*metaDataValue2*/,
                                              uint32_t /*metaDataValue3*/,
                                              uint32_t /*metaDataValue4*/)
{
    static constexpr auto dumpInterface = "com.ibm.Dump.Notify";
    auto& bus = pldm::utils::DBusHandler::getBus();

    auto notifyObjPath = dumpObjPath;
    auto notifyDumpType =
        sdbusplus::common::com::ibm::dump::Notify::DumpType::System;
    if (dumpType == PLDM_FILE_TYPE_RESOURCE_DUMP)
    {
        notifyDumpType =
            sdbusplus::common::com::ibm::dump::Notify::DumpType::Resource;
    }

    try
    {
        auto service = pldm::utils::DBusHandler().getService(notifyObjPath,
                                                             dumpInterface);
        auto method = bus.new_method_call(service.c_str(), notifyObjPath,
                                          dumpInterface, "NotifyDump");
        method.append(fileHandle, length, notifyDumpType, metaDataValue1);
        bus.call(method, dbusTimeout);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error(
            "failed to make a d-bus call to notify a new dump request using newFileAvailableWithMetaData, error - {ERROR}",
            "ERROR", e);
        pldm::utils::reportError(
            "xyz.openbmc_project.PLDM.Error.newFileAvailableWithMetaData.NewDumpNotifyFail");
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
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
        std::string idString = std::format("{:08X}", fileHandle);
        dbusMapping.objectPath = dumpEntryObjPath + idString;
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
            DBusMapping dbusMapping;
            std::string idStr = std::format("{:08X}", fileHandle);
            dbusMapping.objectPath = "/xyz/openbmc_project/dump/system/entry/" +
                                     idStr;
            dbusMapping.interface = "com.ibm.Dump.Entry.Resource";
            dbusMapping.propertyName = "Token";
            dbusMapping.propertyType = "uint32_t";

            pldm::utils::PropertyValue value = metaDataValue1;

            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const std::exception& e)
            {
                error(
                    "failed to set token '{TOKEN}' for resource dump,error - {ERROR}",
                    "TOKEN", metaDataValue1, "ERROR", e);
                return PLDM_ERROR;
            }
        }

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const sdbusplus::exception_t& e)
        {
            error(
                "failed to set DumpRequestStatus property for resource dump entry. error - {ERROR}",
                "ERROR", e);
            return PLDM_ERROR;
        }

        if (statusCode != DumpRequestStatus::Success)
        {
            error("Failue in resource dump file ack with metadata");
            pldm::utils::reportError(
                "xyz.openbmc_project.PLDM.Error.fileAck.ResourceDumpFileAckWithMetaDataFail");

            PropertyValue value{
                "xyz.openbmc_project.Common.Progress.OperationStatus.Failed"};
            DBusMapping dbusMapping{
                dumpEntryObjPath + std::to_string(fileHandle),
                "xyz.openbmc_project.Common.Progress", "Status", "string"};
            try
            {
                pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
            }
            catch (const sdbusplus::exception_t& e)
            {
                error(
                    "Failure in setting Progress as OperationStatus.Failed in fileAckWithMetaData, error - {ERROR}",
                    "ERROR", e);
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
                    "Failed to set the Offloaded dbus property to true, error - {ERROR}",
                    "ERROR", e);
                pldm::utils::reportError(
                    "xyz.openbmc_project.PLDM.Error.fileAckWithMetaData.DumpEntryOffloadedSetFail");
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
} // namespace responder
} // namespace pldm
