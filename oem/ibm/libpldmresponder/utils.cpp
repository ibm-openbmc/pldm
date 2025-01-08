#include "utils.hpp"

#include "common/utils.hpp"
#include "host-bmc/dbus/custom_dbus.hpp"

#include <libpldm/base.h>
#include <libpldm/platform.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Asset/client.hpp>
#include <xyz/openbmc_project/Inventory/Item/Connector/client.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <cstdio>
#include <fstream>

PHOSPHOR_LOG2_USING;

using namespace pldm::utils;

namespace pldm
{

using namespace pldm::dbus;
namespace responder
{
std::mutex lockMutex;

namespace utils
{

using ObjectMapper = sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

static constexpr auto curLicFilePath =
    "/var/lib/pldm/license/current_license.bin";
static constexpr auto newLicFilePath = "/var/lib/pldm/license/new_license.bin";
static constexpr auto newLicJsonFilePath =
    "/var/lib/pldm/license/new_license.json";
static constexpr auto licEntryPath = "/xyz/openbmc_project/license/entry";
static constexpr uint8_t createLic = 1;
static constexpr uint8_t clearLicStatus = 2;

using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

using LicJsonObjMap = std::map<fs::path, nlohmann::json>;
LicJsonObjMap licJsonMap;

int setupUnixSocket(const std::string& socketInterface)
{
    int sock;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t interfaceLength =
        strnlen(socketInterface.c_str(), sizeof(addr.sun_path));

    if (interfaceLength == sizeof(addr.sun_path))
    {
        error("Setup unix socket path '{PATH}' is too long '{LENGTH}'", "PATH",
              socketInterface, "LENGTH", interfaceLength);
        return -1;
    }

    strncpy(addr.sun_path, socketInterface.c_str(), sizeof(addr.sun_path) - 1);

    if ((sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1)
    {
        error("Failed to open unix socket");
        return -errno;
    }

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        error("Failed to bind unix socket, error number - {ERROR_NUM}",
              "ERROR_NUM", errno);
        close(sock);
        return -errno;
    }

    if (listen(sock, 1) == -1)
    {
        error("Failed listen() call while setting up unix socket");
        close(sock);
        return -errno;
    }

    fd_set rfd;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&rfd);
    FD_SET(sock, &rfd);
    int nfd = sock + 1;
    int fd = -1;

    int retval = select(nfd, &rfd, NULL, NULL, &tv);
    if (retval < 0)
    {
        error(
            "Failed select() call while setting up unix socket, error number - {ERROR_NUM}",
            "ERROR_NUM", errno);
        close(sock);
        return -errno;
    }

    if ((retval > 0) && (FD_ISSET(sock, &rfd)))
    {
        fd = accept(sock, NULL, NULL);
        if (fd < 0)
        {
            error(
                "Failed accept() call while setting up unix socket, error number - {ERROR_NUM}",
                "ERROR_NUM", errno);
            close(sock);
            return -errno;
        }
        close(sock);
    }
    return fd;
}

int writeToUnixSocket(const int sock, const char* buf, const uint64_t blockSize)
{
    const std::lock_guard<std::mutex> lock(lockMutex);
    uint64_t i;
    int nwrite = 0;

    for (i = 0; i < blockSize; i = i + nwrite)
    {
        fd_set wfd;
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        FD_ZERO(&wfd);
        FD_SET(sock, &wfd);
        int nfd = sock + 1;

        int retval = select(nfd, NULL, &wfd, NULL, &tv);
        if (retval < 0)
        {
            error(
                "Failed to write to unix socket select, error number - {ERROR_NUM}",
                "ERROR_NUM", errno);
            close(sock);
            return -1;
        }
        if (retval == 0)
        {
            nwrite = 0;
            continue;
        }
        if ((retval > 0) && (FD_ISSET(sock, &wfd)))
        {
            nwrite = write(sock, buf + i, blockSize - i);
            if (nwrite < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                {
                    error(
                        "Failed to write to unix socket, error number - {ERROR_NUM}",
                        "ERROR_NUM", errno);
                    nwrite = 0;
                    continue;
                }
                error(
                    "Failed to write to unix socket, error number - {ERROR_NUM}",
                    "ERROR_NUM", errno);
                close(sock);
                return -1;
            }
        }
        else
        {
            nwrite = 0;
        }
    }
    return 0;
}

Json convertBinFileToJson(const fs::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    std::streampos fileSize;

    // Get the file size
    file.seekg(0, std::ios::end);
    fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read the data into vector from file and convert to json object
    std::vector<uint8_t> vJson(fileSize);
    file.read((char*)&vJson[0], fileSize);
    return Json::from_bson(vJson);
}

void convertJsonToBinaryFile(const Json& jsonData, const fs::path& path)
{
    // Covert the json data to binary format and copy to vector
    std::vector<uint8_t> vJson = {};
    vJson = Json::to_bson(jsonData);

    // Copy the vector to file
    std::ofstream licout(path, std::ios::out | std::ios::binary);
    size_t size = vJson.size();
    licout.write(reinterpret_cast<char*>(&vJson[0]), size * sizeof(vJson[0]));
}

void clearLicenseStatus()
{
    if (!fs::exists(curLicFilePath))
    {
        return;
    }

    auto data = convertBinFileToJson(curLicFilePath);

    const Json empty{};
    const std::vector<Json> emptyList{};

    auto entries = data.value("Licenses", emptyList);
    fs::path path{licEntryPath};

    for (const auto& entry : entries)
    {
        auto licId = entry.value("Id", empty);
        fs::path l_path = path / licId;
        licJsonMap.emplace(l_path, entry);
    }

    createOrUpdateLicenseDbusPaths(clearLicStatus);
}

int createOrUpdateLicenseDbusPaths(const uint8_t& flag)
{
    const Json empty{};
    std::string authTypeAsNoOfDev = "NumberOfDevice";
    struct tm tm;
    time_t licTimeSinceEpoch = 0;

    sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type licType =
        sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type::
            Prototype;
    sdbusplus::com::ibm::License::Entry::server::LicenseEntry::AuthorizationType
        licAuthType = sdbusplus::com::ibm::License::Entry::server::
            LicenseEntry::AuthorizationType::Device;
    for (const auto& [key, licJson] : licJsonMap)
    {
        auto licName = licJson.value("Name", empty);

        auto type = licJson.value("Type", empty);
        if (type == "Trial")
        {
            licType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::Type::Trial;
        }
        else if (type == "Commercial")
        {
            licType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::Type::Purchased;
        }
        else
        {
            licType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::Type::Prototype;
        }

        auto authType = licJson.value("AuthType", empty);
        if (authType == "NumberOfDevice")
        {
            licAuthType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::AuthorizationType::Capacity;
        }
        else if (authType == "Unlimited")
        {
            licAuthType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::AuthorizationType::Unlimited;
        }
        else
        {
            licAuthType = sdbusplus::com::ibm::License::Entry::server::
                LicenseEntry::AuthorizationType::Device;
        }

        uint32_t licAuthDevNo = 0;
        if (authType == authTypeAsNoOfDev)
        {
            licAuthDevNo = licJson.value("AuthDeviceNumber", 0);
        }

        auto licSerialNo = licJson.value("SerialNum", "");

        auto expTime = licJson.value("ExpirationTime", "");
        if (!expTime.empty())
        {
            memset(&tm, 0, sizeof(tm));
            strptime(expTime.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
            licTimeSinceEpoch = mktime(&tm);
        }

        CustomDBus::getCustomDBus().implementLicInterfaces(
            key, licAuthDevNo, licName, licSerialNo, licTimeSinceEpoch, licType,
            licAuthType);

        auto status = licJson.value("Status", empty);

        // License status is a single entry which needs to be mapped to
        // OperationalStatus and Availability dbus interfaces
        auto licOpStatus = false;
        auto licAvailState = false;
        if ((flag == clearLicStatus) || (status == "Unknown"))
        {
            licOpStatus = false;
            licAvailState = false;
        }
        else if (status == "Enabled")
        {
            licOpStatus = true;
            licAvailState = true;
        }
        else if (status == "Disabled")
        {
            licOpStatus = false;
            licAvailState = true;
        }

        CustomDBus::getCustomDBus().setOperationalStatus(key, licOpStatus);
        CustomDBus::getCustomDBus().setAvailabilityState(key, licAvailState);
    }

    return PLDM_SUCCESS;
}

int createOrUpdateLicenseObjs()
{
    bool l_curFilePresent = true;
    const Json empty{};
    const std::vector<Json> emptyList{};
    std::ifstream jsonFileCurrent;
    Json dataCurrent;
    Json entries;

    if (!fs::exists(curLicFilePath))
    {
        l_curFilePresent = false;
    }

    if (l_curFilePresent == true)
    {
        dataCurrent = convertBinFileToJson(curLicFilePath);
    }

    auto dataNew = convertBinFileToJson(newLicFilePath);

    if (l_curFilePresent == true)
    {
        dataCurrent.merge_patch(dataNew);
        convertJsonToBinaryFile(dataCurrent, curLicFilePath);
        entries = dataCurrent.value("Licenses", emptyList);
    }
    else
    {
        convertJsonToBinaryFile(dataNew, curLicFilePath);
        entries = dataNew.value("Licenses", emptyList);
    }

    fs::path path{licEntryPath};

    for (const auto& entry : entries)
    {
        auto licId = entry.value("Id", empty);
        fs::path l_path = path / licId;
        licJsonMap.insert_or_assign(l_path, entry);
    }

    int rc = createOrUpdateLicenseDbusPaths(createLic);
    if (rc == PLDM_SUCCESS)
    {
        fs::copy_file(newLicFilePath, curLicFilePath,
                      fs::copy_options::overwrite_existing);

        if (fs::exists(newLicFilePath))
        {
            // fs::remove_all(newLicFilePath);
        }

        if (fs::exists(newLicJsonFilePath))
        {
            // fs::remove_all(newLicJsonFilePath);
        }
    }

    return rc;
}
std::pair<std::string, std::string>
    getSlotAndAdapter(const std::string& portLocationCode)
{
    std::filesystem::path portPath =
        pldm::responder::utils::getObjectPathByLocationCode(
            portLocationCode, "xyz.openbmc_project.Inventory.Item.Connector");
    return std::make_pair(portPath.parent_path().parent_path(),
                          portPath.parent_path());
}

void hostChapDataIntf(
    pldm::responder::oem_fileio::Handler* dbusToFilehandlerObj)
{
    CustomDBus::getCustomDBus().implementChapDataInterface(
        "/xyz/openbmc_project/pldm", dbusToFilehandlerObj);
}

uint32_t getLinkResetInstanceNumber(std::string& path)
{
    uint32_t id = pldm::dbus::CustomDBus::getCustomDBus().getBusId(path);
    return id;
}

void findSlotObjects(const std::string& boardObjPath,
                     std::vector<std::string>& slotObjects)
{
    static constexpr auto slotInterface =
        "xyz.openbmc_project.Inventory.Item.PCIeSlot";

    auto& bus = pldm::utils::DBusHandler::getBus();
    try
    {
        auto method = bus.new_method_call(
            ObjectMapper::default_service, ObjectMapper::instance_path,
            ObjectMapper::interface, "GetSubTreePaths");
        method.append(boardObjPath);
        method.append(0);
        method.append(std::vector<std::string>({slotInterface}));
        auto reply = bus.call(method);
        reply.read(slotObjects);
    }
    catch (const std::exception& e)
    {
        error("No cec slots under motherboard = {OBJ_PATH}", "OBJ_PATH",
              boardObjPath);
    }
}

std::vector<char> vecSplit(const std::vector<char>& inputVec,
                           const uint32_t startIdx, const uint32_t endIdx)
{
    // Start and end iterators
    auto startItr = inputVec.begin() + startIdx;
    auto endItr = inputVec.begin() + endIdx;

    // Resultant split vector
    std::vector<char> resultVec(endIdx - startIdx + 1);

    copy(startItr, endItr, resultVec.begin());
    return resultVec;
}

void hostPCIETopologyIntf(
    uint8_t mctp_eid,
    pldm::host_effecters::HostEffecterParser* hostEffecterParser)
{
    CustomDBus::getCustomDBus().implementPcieTopologyInterface(
        "/xyz/openbmc_project/pldm", mctp_eid, hostEffecterParser);
}

std::string getObjectPathByLocationCode(const std::string& locationCode,
                                        const std::string& inventoryItemType)
{
    std::string locationIface(
        "xyz.openbmc_project.Inventory.Decorator.LocationCode");
    std::string inventoryIface("xyz.openbmc_project.Inventory.Item");

    std::string path;
    pldm::utils::ObjectValueTree objects;
    try
    {
        objects = pldm::utils::DBusHandler::getInventoryObjects<
            pldm::utils::DBusHandler>();
    }
    catch (const std::exception& e)
    {
        error(
            "Look up of inventory objects failed for location {LOC_CODE} ERROR={ERR_EXCEP}",
            "LOC_CODE", locationCode, "ERR_EXCEP", e);
        return path;
    }

    for (const auto& objPath : objects)
    {
        pldm::utils::InterfaceMap interfaces = objPath.second;
        if (interfaces.contains(inventoryItemType) &&
            interfaces.contains(locationIface))
        {
            pldm::utils::PropertyMap properties = interfaces[locationIface];
            pldm::utils::PropertyMap presentInfo = interfaces[inventoryIface];
            if (properties.contains("LocationCode"))
            {
                if (get<std::string>(properties["LocationCode"]) ==
                    locationCode)
                {
                    // Return the object path only if either
                    // The "xyz.openbmc_project.Inventory.Item" interface is not
                    // present or
                    // The "Present" property is not populated or it is present
                    // and its value is true
                    if (!interfaces.contains(
                            "xyz.openbmc_project.Inventory.Item") ||
                        !presentInfo.contains("Present") ||
                        get<bool>(presentInfo["Present"]))
                    {
                        path = objPath.first.str;
                        return path;
                    }
                }
            }
        }
    }
    error("Location not found {LOC_CODE} for Item type {INVEN_ITEM_TYP}",
          "LOC_CODE", locationCode, "INVEN_ITEM_TYP", inventoryItemType);
    return path;
}

} // namespace utils

namespace oem_ibm_utils
{
using namespace pldm::utils;

int pldm::responder::oem_ibm_utils::Handler::setCoreCount(
    const EntityAssociations& Associations, const EntityMaps entityMaps)
{
    int coreCount = 0;
    // get the CPU pldm entities
    for (const auto& entries : Associations)
    {
        auto parent = pldm_entity_extract(entries[0]);
        // entries[0] would be the parent in the entity association map
        if (parent.entity_type == PLDM_ENTITY_PROC)
        {
            // Need to reset the count for each of the processors
            coreCount = 0;
            for (const auto& entry : entries)
            {
                auto child = pldm_entity_extract(entry);
                if (child.entity_type == (PLDM_ENTITY_PROC | 0x8000))
                {
                    // got a core child
                    ++coreCount;
                }
            }
            try
            {
                auto grand_parent = pldm_entity_get_parent(entries[0]);
                std::string grepWord = std::format(
                    "{}{}/{}{}", entityMaps.at(grand_parent.entity_type),
                    std::to_string(grand_parent.entity_instance_num),
                    entityMaps.at(parent.entity_type),
                    std::to_string(parent.entity_instance_num));
                static constexpr auto searchpath = "/xyz/openbmc_project/";
                std::vector<std::string> cpuInterface = {
                    "xyz.openbmc_project.Inventory.Item.Cpu"};
                pldm::utils::GetSubTreeResponse response = dBusIntf->getSubtree(
                    searchpath, 0 /* depth */, cpuInterface);
                for (const auto& [objectPath, serviceMap] : response)
                {
                    // find the object path with first occurance of coreX
                    if (objectPath.contains(grepWord))
                    {
                        pldm::utils::DBusMapping dbusMapping{
                            objectPath, cpuInterface[0], "CoreCount",
                            "uint16_t"};
                        pldm::utils::PropertyValue value =
                            static_cast<uint16_t>(coreCount);
                        try
                        {
                            dBusIntf->setDbusProperty(dbusMapping, value);
                        }
                        catch (const std::exception& e)
                        {
                            error(
                                "Failed to set the core count property at interface '{INTERFACE}': {ERROR}",
                                "INTERFACE", cpuInterface[0], "ERROR", e);
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                error("Failed to searching CoreCount property: {ERROR}",
                      "ERROR", e);
            }
        }
    }
    return coreCount;
}

bool pldm::responder::oem_ibm_utils::Handler::checkIfIBMFru(
    const std::string& objPath)
{
    using DecoratorAsset =
        sdbusplus::client::xyz::openbmc_project::inventory::decorator::Asset<>;

    try
    {
        auto propVal = pldm::utils::DBusHandler().getDbusPropertyVariant(
            objPath.c_str(), "Model", DecoratorAsset::interface);
        const auto& model = std::get<std::string>(propVal);
        if (!model.empty())
        {
            return true;
        }
    }
    catch (const std::exception&)
    {
        return false;
    }
    return false;
}

bool pldm::responder::oem_ibm_utils::Handler::checkModelPresence(
    const std::string& objPath)
{
    constexpr auto pcieAdapterModelInterface =
        "xyz.openbmc_project.Inventory.Decorator.Asset";
    constexpr auto modelProperty = "Model";

    try
    {
        auto propVal = pldm::utils::DBusHandler().getDbusPropertyVariant(
            objPath.c_str(), modelProperty, pcieAdapterModelInterface);
        const auto& model = std::get<std::string>(propVal);
        if (!model.empty())
        {
            return true;
        }
    }
    catch (const sdbusplus::exception::SdBusError&)
    {
        return false;
    }
    return false;
}

bool pldm::responder::oem_ibm_utils::Handler::checkFruPresence(
    const char* objPath)
{
    // if we enter here with port, then we need to find the
    // parent and see if the pcie card or the drive bp is present. if so then
    // the port is considered as present. this is so because
    // the ports do not have "Present" property
    std::string pcieAdapter("pcie_card");
    std::string nvmeAdapter("_drive");
    std::string portStr("cxp_");
    std::string connectorStr("_connector");
    std::string newObjPath = objPath;
    bool isPresent = true;

    if ((newObjPath.find(pcieAdapter) != std::string::npos) ||
        (newObjPath.find(nvmeAdapter) != std::string::npos))
    {
        if ((newObjPath.find(portStr) != std::string::npos) ||
            (newObjPath.find(connectorStr) != std::string::npos))
        {
            newObjPath = pldm::utils::findParent(objPath);
        }
        else
        {
            // Adapter PDRs are always created underneath the slot for
            // Insustry standard cards as well as IBM cable cards.
            // Phyp expects the FRU records for industry std cards to be always
            // built, irrespective of presence. This is needed by PHYP to send
            // the Fan floor information
            return true; // industry std cards/IBM cable cards
        }
    }

    static constexpr auto presentInterface =
        "xyz.openbmc_project.Inventory.Item";
    static constexpr auto presentProperty = "Present";
    try
    {
        auto propVal = pldm::utils::DBusHandler().getDbusPropertyVariant(
            newObjPath.c_str(), presentProperty, presentInterface);
        isPresent = std::get<bool>(propVal);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        error(
            "D-Bus method call to get the FRU presence of {FRU} failed ERROR={ERR} and return is {V}",
            "FRU", newObjPath, "ERR", e.what(), "V", (int)isPresent);
    }
    return isPresent;
}

std::vector<std::string>
    pldm::responder::oem_ibm_utils::Handler::findPortObjects(
        const std::string& adapterObjPath)
{
    using ItemConnector =
        sdbusplus::client::xyz::openbmc_project::inventory::item::Connector<>;

    std::vector<std::string> portObjects;
    try
    {
        portObjects = pldm::utils::DBusHandler().getSubTreePaths(
            adapterObjPath, 0,
            std::vector<std::string>({ItemConnector::interface}));
    }
    catch (const std::exception& e)
    {
        error("No ports under adapter '{ADAPTER_OBJ_PATH}'  - {ERROR}.",
              "ADAPTER_OBJ_PATH", adapterObjPath.c_str(), "ERROR", e);
    }

    return portObjects;
}

} // namespace oem_ibm_utils
} // namespace responder
} // namespace pldm
