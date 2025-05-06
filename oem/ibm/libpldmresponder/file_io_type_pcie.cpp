#include "file_io_type_pcie.hpp"

#include "common/utils.hpp"
#include "host-bmc/dbus/custom_dbus.hpp"
#include "host-bmc/dbus/serialize.hpp"
#include "utils.hpp"

#include <libpldm/base.h>
#include <libpldm/oem/ibm/file_io.h>
#include <sys/stat.h>

#include <phosphor-logging/lg2.hpp>

#include <cstdint>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace responder
{

std::unordered_map<uint8_t, std::string> linkStateMap{
    {0x00, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Operational"},
    {0x01, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Degraded"},
    {0x02, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0x03, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0x04, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Failed"},
    {0x05, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Open"},
    {0x06, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Inactive"},
    {0x07, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0xFF, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unknown"}};

std::unordered_map<uint8_t, std::string> linkSpeed{
    {0x00, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen1"},
    {0x01, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen2"},
    {0x02, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen3"},
    {0x03, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen4"},
    {0x04, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen5"},
    {0x10, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Unknown"},
    {0xFF, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Unknown"}};

std::unordered_map<uint8_t, size_t> linkWidth{
    {0x01, 1},  {0x02, 2},        {0x04, 4}, {0x08, 8},
    {0x10, 16}, {0xFF, UINT_MAX}, {0x00, 0}};

std::unordered_map<uint8_t, double> cableLengthMap{
    {0x00, 0},  {0x01, 2},  {0x02, 3},
    {0x03, 10}, {0x04, 20}, {0xFF, std::numeric_limits<double>::quiet_NaN()}};

std::unordered_map<uint8_t, std::string> cableTypeMap{
    {0x00, "optical"}, {0x01, "copper"}, {0xFF, "Unknown"}};

std::unordered_map<uint8_t, std::string> cableStatusMap{
    {0x00, "xyz.openbmc_project.Inventory.Item.Cable.Status.Inactive"},
    {0x01, "xyz.openbmc_project.Inventory.Item.Cable.Status.Running"},
    {0x02, "xyz.openbmc_project.Inventory.Item.Cable.Status.PoweredOff"},
    {0xFF, "xyz.openbmc_project.Inventory.Item.Cable.Status.Unknown"}};

static constexpr auto pciePath = "/var/lib/pldm/pcie-topology/";
constexpr auto topologyFile = "topology";
constexpr auto cableInfoFile = "cableinfo";

constexpr auto itemPCIeSlot = "xyz.openbmc_project.Inventory.Item.PCIeSlot";
constexpr auto itemPCIeDevice = "xyz.openbmc_project.Inventory.Item.PCIeDevice";
constexpr auto itemConnector = "xyz.openbmc_project.Inventory.Item.Connector";

// Slot location code structure contains multiple slot location code
// suffix structures.
// Each slot location code suffix structure is as follows
// {Slot location code suffix size(uint8_t),
//  Slot location code suffix(variable size)}
constexpr auto sizeOfSuffixSizeDataMember = 1;

// Each slot location structure contains
// {
//   Number of slot location codes (1byte),
//   Slot location code Common part size (1byte)
//   Slot location common part (Var)
// }
constexpr auto slotLocationDataMemberSize = 2;

namespace fs = std::filesystem;

std::unordered_map<uint16_t, bool> PCIeInfoHandler::receivedFiles;
std::unordered_map<LinkId, std::tuple<LinkStatus, linkTypeData, LinkSpeed,
                                      LinkWidth, PcieHostBridgeLoc, LocalPort,
                                      RemotePort, IoSlotLocation, LinkId>>
    PCIeInfoHandler::topologyInformation;
std::unordered_map<
    CableLinkNum, std::tuple<LinkId, LocalPortLocCode, IoSlotLocationCode,
                             CablePartNum, CableLength, CableType, CableStatus>>
    PCIeInfoHandler::cableInformation;

std::map<std::string,
         std::tuple<uint16_t, std::string, std::optional<std::string>>>
    PCIeInfoHandler::mexObjectMap;

std::vector<std::string> PCIeInfoHandler::cables;

std::unordered_map<LinkId, linkTypeData> PCIeInfoHandler::linkTypeInfo;
std::vector<std::pair<LinkId, LinkId>> PCIeInfoHandler::needPostProcessing;

PCIeInfoHandler::PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType) :
    FileHandler(fileHandle), infoType(fileType)
{
    deleteTopologyFiles();
    receivedFiles.emplace(infoType, false);
}

void PCIeInfoHandler::writeFromMemory(
    uint32_t offset, uint32_t length, uint64_t address,
    oem_platform::Handler* /*oemPlatformHandler*/,
    SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event)
{
    if (!fs::exists(pciePath))
    {
        fs::create_directories(pciePath);
        fs::permissions(pciePath,
                        fs::perms::others_read | fs::perms::owner_write);
    }

    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if (infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
        infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    try
    {
        std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary);
        if (!pcieData)
        {
            error("PCIe Info file creation error ");
            FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                     PLDM_ERROR, 0);
            FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
            return;
        }
        transferFileData(infoFile, false, offset, length, address,
                         sharedAIORespDataobj, event);
    }
    catch (const std::exception& e)
    {
        FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                 PLDM_ERROR, 0);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        error("Create/Write data to the File type '{TYPE}' failed with {ERROR}",
              "TYPE", (int)infoType, "ERROR", e);
        return;
    }
}

int PCIeInfoHandler::write(const char* buffer, uint32_t, uint32_t& length,
                           oem_platform::Handler* /*oemPlatformHandler*/,
                           struct fileack_status_metadata& /*metaDataObj*/)
{
    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if (infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
        infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    try
    {
        std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary |
                                             std::ios::app);

        if (buffer)
        {
            pcieData.write(buffer, length);
        }
        pcieData.close();
    }
    catch (const std::exception& e)
    {
        error("Create/Write data to the File type {TYPE}, failed {ERROR}",
              "TYPE", infoType, "ERROR", e);
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int PCIeInfoHandler::fileAck(uint8_t /*fileStatus*/)
{
    if (receivedFiles.find(infoType) == receivedFiles.end())
    {
        error("Received FileAck for the file which is not received");
    }
    receivedFiles[infoType] = true;
    try
    {
        if (receivedFiles.contains(PLDM_FILE_TYPE_CABLE_INFO) &&
            receivedFiles.contains(PLDM_FILE_TYPE_PCIE_TOPOLOGY))
        {
            if (receivedFiles.at(PLDM_FILE_TYPE_CABLE_INFO) &&
                receivedFiles.at(PLDM_FILE_TYPE_PCIE_TOPOLOGY))
            {
                receivedFiles.clear();
                // parse the topology data and cache the information
                // for further processing
                parseTopologyData();

                getMexObjects();

                // delete the cable dbus as the cable are dynamic
                for (const auto& cable : cables)
                {
                    error("Removing cable object [ {CABLE} ]", "CABLE", cable);
                    pldm::dbus::CustomDBus::getCustomDBus().deleteObject(cable);
                }
                cables.clear();

                // set topology properties & host cable dbus objects
                setTopologyAttrsOnDbus();
                // even when we fail to parse /set the topology infromation on
                // to the dbus, we need to set the property back to false - to
                // allow the redfish/user to be able to ask the topology
                // information again
                pldm::dbus::CustomDBus::getCustomDBus().updateTopologyProperty(
                    false);

                // clear all the cached information
                clearTopologyInfo();
            }
        }
    }
    catch (const std::out_of_range& e)
    {
        info("Received only one of the topology file");
    }
    return PLDM_SUCCESS;
}

void PCIeInfoHandler::setProperty(
    const std::string& objPath, const std::string& propertyName,
    const pldm::utils::PropertyValue& propertyValue,
    const std::string& interfaceName, const std::string& propertyType)
{
    pldm::utils::PropertyValue value = propertyValue;
    pldm::utils::DBusMapping dbusMapping;
    dbusMapping.objectPath = objPath;
    dbusMapping.interface = interfaceName;
    dbusMapping.propertyName = propertyName;
    dbusMapping.propertyType = propertyType;
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error(
            "Failed To set {PROP_NAME} property and path :{OBJ_PATH}, ERROR={ERR_EXCEP}",
            "PROP_NAME", propertyName, "OBJ_PATH", objPath.c_str(), "ERR_EXCEP",
            e);
    }
}

void PCIeInfoHandler::getMexObjects()
{
    // get the in memory cached copy of mex objects
    auto savedObjs = pldm::serialize::Serialize::getSerialize().getSavedObjs();

    // Find the PCIE slots & PCIE logical slots & PCIeAdapters & Connecters &
    // their location codes
    std::set<uint16_t> neededEntityTypes = {
        PLDM_ENTITY_SLOT, PLDM_ENTITY_CARD, PLDM_ENTITY_CONNECTOR,
        PLDM_ENTITY_SYSTEM_CHASSIS, (0x8000 | PLDM_ENTITY_SLOT)};
    std::set<std::string> neededProperties = {"locationCode"};

    for (const auto& [entityType, objects] : savedObjs)
    {
        if (neededEntityTypes.contains(entityType))
        {
            for (const auto& [objectPath, object] : objects)
            {
                const auto& [instanceId, conainerId, obj] = object;
                for (const auto& [interfaces, propertyValue] : obj)
                {
                    for (const auto& [propertyName, value] : propertyValue)
                    {
                        if (neededProperties.contains(propertyName))
                        {
                            if (propertyName == "locationCode")
                            {
                                mexObjectMap.insert_or_assign(
                                    objectPath,
                                    std::make_tuple(
                                        entityType, propertyName,
                                        std::get<std::string>(value)));
                            }
                        }
                        else
                        {
                            if (propertyName == "present" &&
                                entityType == PLDM_ENTITY_CARD)
                            {
                                mexObjectMap.insert_or_assign(
                                    objectPath,
                                    std::make_tuple(entityType, propertyName,
                                                    std::nullopt));
                            }
                        }
                    }
                }
            }
        }
    }
}

std::string PCIeInfoHandler::getMexObjectFromLocationCode(
    const std::string& locationCode, uint16_t entityType)
{
    for (const auto& [objectPath, obj] : mexObjectMap)
    {
        // Added the check for logical entity type
        if (((std::get<0>(obj) == entityType) ||
             (std::get<0>(obj) == (0x8000 | entityType))) &&
            (std::get<1>(obj) == "locationCode"))
        {
            if ((std::get<2>(obj)).has_value() &&
                (std::get<2>(obj)).value() == locationCode)
            {
                return objectPath;
            }
        }
    }
    return "";
}

std::string PCIeInfoHandler::getAdapterFromSlot(
    const std::string& mexSlotObject)
{
    for (const auto& [objectPath, obj] : mexObjectMap)
    {
        if (objectPath.find(mexSlotObject) != std::string::npos)
        {
            if (std::get<0>(obj) == PLDM_ENTITY_CARD)
            {
                return objectPath;
            }
        }
    }
    return "";
}

std::string PCIeInfoHandler::getDownStreamChassis(
    const std::string& slotOrConnecterPath)
{
    for (const auto& [objectPath, obj] : mexObjectMap)
    {
        if (slotOrConnecterPath.find(objectPath) != std::string::npos)
        {
            // found objectpath in slot or connecter
            if (std::get<0>(obj) == PLDM_ENTITY_SYSTEM_CHASSIS)
            {
                return objectPath;
            }
        }
    }
    return "";
}

std::pair<std::string, std::string> PCIeInfoHandler::getMexSlotandAdapter(
    const std::filesystem::path& connector)
{
    return std::make_pair(connector.parent_path().parent_path(),
                          connector.parent_path());
}

void PCIeInfoHandler::setTopologyOnSlotAndAdapter(
    uint8_t linkType, const std::pair<std::string, std::string>& slotAndAdapter,
    const uint32_t& linkId, const std::string& linkStatus, uint8_t curLinkSpeed,
    int64_t linkWidth, bool isHostedByPLDM)
{
    if (!slotAndAdapter.first.empty())
    {
        if (linkType == static_cast<uint8_t>(linkTypeData::Primary))
        {
            // we got the slot dbus object, set linkid, linkstatus on it
            // set the busid on the respective slots
            setProperty(slotAndAdapter.first, "BusId", linkId, itemPCIeSlot,
                        "uint32_t");

            // set the link status on the respective slots
            setProperty(slotAndAdapter.first, "LinkStatus", linkStatus,
                        itemPCIeSlot, "string");

            std::filesystem::path slot(slotAndAdapter.first);

            error(
                "Primary Link, BusId - [{BUS_ID}] LinkStatus - [{LINK_STATUS}] on [{FILE_NAME}]",
                "BUS_ID", lg2::hex, linkId, "LINK_STATUS", linkStatus,
                "FILE_NAME", slot.filename());
        }
        else
        {
            if (isHostedByPLDM)
            {
                // its a secondary link so update the busid & link status on the
                // mex slot hosted object by pldm
                pldm::dbus::CustomDBus::getCustomDBus().setSlotProperties(
                    slotAndAdapter.first, linkId, linkStatus);
            }
            else
            {
                // its a secondary link , but its an nvme slot hosted by
                // inventory manager
                setProperty(slotAndAdapter.first, "BusId", linkId, itemPCIeSlot,
                            "uint32_t");

                // set the link status on the respective slots
                setProperty(slotAndAdapter.first, "LinkStatus", linkStatus,
                            itemPCIeSlot, "string");
            }

            std::filesystem::path slot(slotAndAdapter.first);
            std::filesystem::path printSlot = slot.parent_path().filename();
            printSlot /= slot.filename();

            error(
                "Secondary Link, BusId - [{BUS_ID}] LinkStatus - [{LINK_STATUS}] on [{PRINT_SLOT}]",
                "BUS_ID", lg2::hex, linkId, "LINK_STATUS", linkStatus,
                "PRINT_SLOT", printSlot);
        }
    }
    if (!slotAndAdapter.second.empty())
    {
        if (linkType == static_cast<uint8_t>(linkTypeData::Primary))
        {
            // we got the adapter dbus object, set linkspeed & linkwidth on it
            setProperty(slotAndAdapter.second, "GenerationInUse",
                        linkSpeed[curLinkSpeed], itemPCIeDevice, "string");

            // set link width
            auto& bus = pldm::utils::DBusHandler::getBus();
            auto service = pldm::utils::DBusHandler().getService(
                slotAndAdapter.second.c_str(), itemPCIeDevice);
            auto method = bus.new_method_call(
                service.c_str(), slotAndAdapter.second.c_str(),
                "org.freedesktop.DBus.Properties", "Set");
            method.append(itemPCIeDevice, "LanesInUse", linkWidth);

            std::filesystem::path adapter(slotAndAdapter.second);

            error(
                "Primary Link, GenerationInUse - [{LINK_SPEED}] LanesInUse - [{LINK_WIDTH}] on [{ADAPTER_FILE}]",
                "LINK_SPEED", linkSpeed[curLinkSpeed], "LINK_WIDTH", linkWidth,
                "ADAPTER_FILE", adapter.filename());
        }
        else
        {
            if (isHostedByPLDM)
            {
                //  its a secondary link so update the link speed on mex adapter
                if (linkStatus ==
                        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Open" ||
                    linkStatus ==
                        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unknown")
                {
                    // There is no adapter plugged in so set the adapter present
                    // property to false
                    pldm::dbus::CustomDBus::getCustomDBus()
                        .updateInventoryItemProperties(slotAndAdapter.second,
                                                       false);
                }
                else if (
                    linkStatus ==
                    "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Operational")
                {
                    // Thre is adapter plugged in , its powered on and is
                    // operational, so change the present property of the
                    // adapter to true
                    pldm::dbus::CustomDBus::getCustomDBus()
                        .updateInventoryItemProperties(slotAndAdapter.second,
                                                       true);
                }
                pldm::dbus::CustomDBus::getCustomDBus().setPCIeDeviceProps(
                    slotAndAdapter.second, linkWidth, linkSpeed[curLinkSpeed]);
            }
            else
            {
                // we got the adapter dbus object, set linkspeed & linkwidth on
                // it
                setProperty(slotAndAdapter.second, "GenerationInUse",
                            linkSpeed[curLinkSpeed], itemPCIeDevice, "string");

                // set link width
                auto& bus = pldm::utils::DBusHandler::getBus();
                auto service = pldm::utils::DBusHandler().getService(
                    slotAndAdapter.second.c_str(), itemPCIeDevice);
                auto method = bus.new_method_call(
                    service.c_str(), slotAndAdapter.second.c_str(),
                    "org.freedesktop.DBus.Properties", "Set");
                method.append(itemPCIeDevice, "LanesInUse", linkWidth);
            }

            std::filesystem::path adapter(slotAndAdapter.second);
            std::filesystem::path printAdapter =
                adapter.parent_path().parent_path().filename();
            printAdapter /= adapter.parent_path().filename();
            printAdapter /= adapter.filename();

            error(
                "Secondary Link, GenerationInUse - [{LINK_SPEED}] LanesInUse - [{LINK_WIDTH}] on [{PRNT_ADAPTER}]",
                "LINK_SPEED", linkSpeed[curLinkSpeed], "LINK_WIDTH", linkWidth,
                "PRNT_ADAPTER", printAdapter);
        }
    }
}

void PCIeInfoHandler::parsePrimaryLink(
    uint8_t linkType, const IoSlotLocation& ioSlotLocationCode,
    const LocalPort& localPortLocation, const uint32_t& linkId,
    const std::string& linkStatus, uint8_t curLinkSpeed, int64_t linkWidth,
    uint8_t parentLinkId)
{
    // Check the io_slot_location_code size
    if (!ioSlotLocationCode.size())
    {
        // If there is no io slot location code populated
        // then its a cable card that in plugged into one of the
        // cec slots , but the other end is not plugged into the
        // mex drawer.

        // check the local port and from there obtain the
        // pcie slot information
        if (!localPortLocation.first.empty())
        {
            // the local port field can be filled up with slot location
            // code (for the links that have connections to flett)
            if (localPortLocation.first.find("-T") != std::string::npos)
            {
                // the top local port is populated with the connector
                auto slotAndAdapter = pldm::responder::utils::getSlotAndAdapter(
                    localPortLocation.first);
                setTopologyOnSlotAndAdapter(linkType, slotAndAdapter, linkId,
                                            linkStatus, curLinkSpeed, linkWidth,
                                            false);
            }
            else
            {
                // the slot location code is present in the local port
                // location code field
                std::string slotObjectPath =
                    pldm::responder::utils::getObjectPathByLocationCode(
                        localPortLocation.first, itemPCIeSlot);

                // get the adapter with the same location code
                std::string adapterObjPath =
                    pldm::responder::utils::getObjectPathByLocationCode(
                        localPortLocation.first, itemPCIeDevice);

                // set topology info on both the slot and adapter object
                setTopologyOnSlotAndAdapter(
                    linkType, std::make_pair(slotObjectPath, adapterObjPath),
                    linkId, linkStatus, curLinkSpeed, linkWidth, false);
            }
        }
    }
    else if (ioSlotLocationCode.size() == 1)
    {
        // If there is just a single slot location code populated
        // then its a clean link that tells that something is
        // plugged into a CEC PCIeSlot
        std::string slotObjectPath =
            pldm::responder::utils::getObjectPathByLocationCode(
                ioSlotLocationCode[0], itemPCIeSlot);

        // get the adapter with the same location code
        std::string adapterObjPath =
            pldm::responder::utils::getObjectPathByLocationCode(
                ioSlotLocationCode[0], itemPCIeDevice);

        // set topology info on both the slot and adapter object
        setTopologyOnSlotAndAdapter(
            linkType, std::make_pair(slotObjectPath, adapterObjPath), linkId,
            linkStatus, curLinkSpeed, linkWidth, false);
    }
    else
    {
        // if we enter here, that means its a primary link that involves pcie
        // slot connected to a pcie switch or a flett. This link would need
        // additional processing - as we need to check the parent link of this
        // to figure out the slot and the adapter
        if (!localPortLocation.first.empty())
        {
            // we have a io slot location code (probably mex or cec), but its a
            // primary link and if we have local ports , then figure out which
            // slot it is connected to
            if (localPortLocation.first.find("-T") != std::string::npos)
            {
                auto slotAndAdapter = pldm::responder::utils::getSlotAndAdapter(
                    localPortLocation.first);

                setTopologyOnSlotAndAdapter(linkType, slotAndAdapter, linkId,
                                            linkStatus, curLinkSpeed, linkWidth,
                                            false);
            }
            else
            {
                // check if it is a cec slot first
                std::string slotObjectPath =
                    pldm::responder::utils::getObjectPathByLocationCode(
                        localPortLocation.first, itemPCIeSlot);

                // get the adapter with the same location code
                std::string adapterObjPath =
                    pldm::responder::utils::getObjectPathByLocationCode(
                        localPortLocation.first, itemPCIeDevice);

                if (slotObjectPath.empty())
                {
                    // check if it is a mex slot
                    // if its a slot, then
                    slotObjectPath =
                        pldm::responder::utils::getObjectPathByLocationCode(
                            ioSlotLocationCode[0], itemPCIeSlot);

                    // get the adapter with the same location code
                    adapterObjPath =
                        pldm::responder::utils::getObjectPathByLocationCode(
                            ioSlotLocationCode[0], itemPCIeDevice);
                }

                // set topology info on both the slot and adapter object
                setTopologyOnSlotAndAdapter(
                    linkType, std::make_pair(slotObjectPath, adapterObjPath),
                    linkId, linkStatus, curLinkSpeed, linkWidth, false);
            }
        }
        else
        {
            // If there is no local port populated & the io slots are multiple,
            // we would need futher processing for this link
            needPostProcessing.emplace_back(linkId, parentLinkId);
        }
    }
}

void PCIeInfoHandler::parseSecondaryLink(
    uint8_t linkType, const IoSlotLocation& ioSlotLocationCode,
    const LocalPort& /*localPortLocation*/,
    const RemotePort& remotePortLocation, const uint32_t& linkId,
    const std::string& linkStatus, uint8_t curLinkSpeed, int64_t linkWidth)
{
    if (ioSlotLocationCode.size() == 1)
    {
        // if the slot location code is present, it can be either a mex slot or
        // an nvme slot
        std::string mexSlotpath = getMexObjectFromLocationCode(
            ioSlotLocationCode[0], PLDM_ENTITY_SLOT);

        if (!mexSlotpath.empty())
        {
            std::string mexAdapterpath = getAdapterFromSlot(mexSlotpath);
            if (!mexAdapterpath.empty())
            {
                setTopologyOnSlotAndAdapter(
                    linkType, std::make_pair(mexSlotpath, mexAdapterpath),
                    linkId, linkStatus, curLinkSpeed, linkWidth, true);
            }
        }
        else
        {
            // its not a mex slot , check if it matches with any of the CEC nvme
            // slots
            std::string slotObjectPath =
                pldm::responder::utils::getObjectPathByLocationCode(
                    ioSlotLocationCode[0], itemPCIeSlot);

            // get the adapter with the same location code
            std::string adapterObjPath =
                pldm::responder::utils::getObjectPathByLocationCode(
                    ioSlotLocationCode[0], itemPCIeDevice);

            // set topology info on both the slot and adapter object
            setTopologyOnSlotAndAdapter(
                linkType, std::make_pair(slotObjectPath, adapterObjPath),
                linkId, linkStatus, curLinkSpeed, linkWidth, false);
        }
    }
    else if (ioSlotLocationCode.size() > 1)
    {
        // its a secondary link (first one that explains that the link is
        // between a pcie switch in side the cable card to a mex drawer use the
        // remote port location code to figure out the mex slot

        std::string mexConnecterPath = getMexObjectFromLocationCode(
            remotePortLocation.first, PLDM_ENTITY_CONNECTOR);

        std::filesystem::path mexConnecter(mexConnecterPath);
        auto mexSlotandAdapter = getMexSlotandAdapter(mexConnecter);

        info("Creating associations under {MEX_SLOT_ADAPTER}",
             "MEX_SLOT_ADAPTER", std::get<1>(mexSlotandAdapter));
        std::vector<std::tuple<std::string, std::string, std::string>>
            associations;
        for (auto slot : ioSlotLocationCode)
        {
            associations.emplace_back(
                "containing", "contained_by",
                getMexObjectFromLocationCode(slot, PLDM_ENTITY_SLOT));
        }

        pldm::dbus::CustomDBus::getCustomDBus().setAssociations(
            std::get<1>(mexSlotandAdapter), associations);

        // set topology info on both the slot and adapter object
        setTopologyOnSlotAndAdapter(linkType, mexSlotandAdapter, linkId,
                                    linkStatus, curLinkSpeed, linkWidth, true);
    }
}

void PCIeInfoHandler::parseSpeciallink(LinkId linkId, LinkId /*parentLinkId*/)
{
    auto linkInfo = topologyInformation[linkId];
    for (const auto& [link, info] : topologyInformation)
    {
        // if any link has linkId(that link that needs post processing) as the
        // parent link id, then check if that link has local port location codes
        // populated
        if (std::get<8>(info) == linkId)
        {
            auto localPortLocCode = std::get<5>(info);
            if (!localPortLocCode.first.empty())
            {
                // local port location code is present, using the port to find
                // out the slot
                auto slotAndAdapter = pldm::responder::utils::getSlotAndAdapter(
                    localPortLocCode.first);
                error("Special processing for link - [{LINK_ID}]", "LINK_ID",
                      linkId);
                setTopologyOnSlotAndAdapter(
                    static_cast<uint8_t>(std::get<1>(linkInfo)), slotAndAdapter,
                    linkId, std::get<0>(linkInfo), std::get<2>(linkInfo),
                    std::get<3>(linkInfo), false);
            }
        }
    }
}

void PCIeInfoHandler::setTopologyAttrsOnDbus()
{
    // Core Topology Algorithm
    // Iterate through each link and set the link attributes
    for (const auto& [link, info] : topologyInformation)
    {
        // Link type can be either Primary/Secondary/Unkown
        switch (std::get<1>(info))
        {
                // If the link is primary
            case linkTypeData::Primary:
                parsePrimaryLink(static_cast<uint8_t>(std::get<1>(info)),
                                 std::get<7>(info), std::get<5>(info),
                                 static_cast<uint32_t>(link), std::get<0>(info),
                                 std::get<2>(info), std::get<3>(info),
                                 std::get<8>(info));
                break;
            case linkTypeData::Secondary:
                parseSecondaryLink(
                    static_cast<uint8_t>(std::get<1>(info)), std::get<7>(info),
                    std::get<5>(info), std::get<6>(info),
                    static_cast<uint32_t>(link), std::get<0>(info),
                    std::get<2>(info), std::get<3>(info));
                break;
            case linkTypeData::Unknown:
                error("link type is unkown : {LINK}", "LINK", (unsigned)link);
                switch (linkTypeInfo[link])
                {
                    case linkTypeData::Primary:
                        parsePrimaryLink(
                            static_cast<uint8_t>(linkTypeInfo[link]),
                            std::get<7>(info), std::get<5>(info),
                            static_cast<uint32_t>(link), std::get<0>(info),
                            std::get<2>(info), std::get<3>(info),
                            std::get<8>(info));
                        break;
                    case linkTypeData::Secondary:
                        parseSecondaryLink(
                            static_cast<uint8_t>(linkTypeInfo[link]),
                            std::get<7>(info), std::get<5>(info),
                            std::get<6>(info), static_cast<uint32_t>(link),
                            std::get<0>(info), std::get<2>(info),
                            std::get<3>(info));
                        break;
                    default:
                        break;
                }
            default:
                break;
        }
    }

    // There are few special links that needs post processing
    for (const auto& link : needPostProcessing)
    {
        parseSpeciallink(link.first, link.second);
    }

    std::filesystem::path cableBasePath =
        "/xyz/openbmc_project/inventory/system";
    // Create Cable Dbus objects
    for (const auto& [cable_no, info] : cableInformation)
    {
        std::filesystem::path cableObjectPath(cableBasePath);
        cableObjectPath /= "external_cable_" + std::to_string(cable_no);

        // Implement Item.Cable Interface on it
        pldm::dbus::CustomDBus::getCustomDBus().implementCableInterface(
            cableObjectPath.string());

        // Implement Inventory.Item Interface on it
        pldm::dbus::CustomDBus::getCustomDBus().updateInventoryItemProperties(
            cableObjectPath.string(), true);

        // Implement Inventory.Decorator.Asset on it
        pldm::dbus::CustomDBus::getCustomDBus().implementAssetInterface(
            cableObjectPath.string());

        // set the part Number on the asset interface
        pldm::dbus::CustomDBus::getCustomDBus().setPartNumber(
            cableObjectPath.string(), std::get<3>(info));

        // Set all the cable attributes on the object
        pldm::dbus::CustomDBus::getCustomDBus().setCableAttributes(
            cableObjectPath.string(), std::get<4>(info), std::get<5>(info),
            std::get<6>(info));

        // Frame the necessary associations on each cable
        // upstream port - connecter on CEC pcie adapter
        // downstream port - connecter on mex io module
        // downstream chassis - the mex chassis this cable is plugged into
        auto upstreamInformation =
            pldm::responder::utils::getObjectPathByLocationCode(
                std::get<1>(info), itemConnector);
        auto downstreamInformation = getMexObjectFromLocationCode(
            std::get<2>(info), PLDM_ENTITY_CONNECTOR);
        auto downstreamChassis = getDownStreamChassis(downstreamInformation);
        std::vector<std::tuple<std::string, std::string, std::string>>
            associations{
                {"upstream_connector", "attached_cables", upstreamInformation},
                {"downstream_connector", "attached_cables",
                 downstreamInformation},
                {"downstream_chassis", "attached_cables", downstreamChassis}};

        pldm::dbus::CustomDBus::getCustomDBus().setAssociations(
            cableObjectPath.string(), associations);
        error(
            "Hosted Cable [ {CABLE_NO} ] Length - [ {LEN} ] Type - [ {CABLE_TYP} ] Status - [ {CABLE_STATUS} ] PN - [ {PN} ]",
            "CABLE_NO", cable_no, "LEN", std::get<4>(info), "CABLE_TYP",
            std::get<5>(info), "CABLE_STATUS", std::get<6>(info), "PN",
            std::get<3>(info));
        error(
            "Hosted Cable [ {CABLE_NO} ] UPConnector - [ {UP_CONN} ] DNConnector - [ {DN_CONN} ] DChassis - [ {D_CHASSIS} ]",
            "CABLE_NO", cable_no, "UP_CONN", upstreamInformation, "DN_CONN",
            downstreamInformation, "D_CHASSIS", downstreamChassis);
        cables.push_back(cableObjectPath.string());
    }
}

void PCIeInfoHandler::clearTopologyInfo()
{
    topologyInformation.clear();

    cableInformation.clear();

    mexObjectMap.clear();

    needPostProcessing.clear();
}

void PCIeInfoHandler::parseTopologyData()
{
    int fd = open((fs::path(pciePath) / topologyFile).string().c_str(),
                  O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        perror("Topology file not present");
        return;
    }
    pldm::responder::utils::CustomFD topologyFd(fd);
    // Reading the statistics of the topology file, to get the size.
    // stat sb is the out parameter provided to fstat
    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        perror("Could not get topology file size");
        return;
    }

    if (sb.st_size == 0)
    {
        error("Topology file Size is 0");
        return;
    }

    auto topologyCleanup = [sb](void* fileInMemory) {
        munmap(fileInMemory, sb.st_size);
    };

    // memory map the topology file into pldm memory
    void* fileInMemory =
        mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, topologyFd(), 0);
    if (MAP_FAILED == fileInMemory)
    {
        error("mmap on topology file failed with error {RC}", "RC", -errno);
        return;
    }

    std::unique_ptr<void, decltype(topologyCleanup)> topologyPtr(
        fileInMemory, topologyCleanup);

    auto pcieLinkList = reinterpret_cast<struct topologyBlob*>(fileInMemory);
    uint16_t numOfLinks = 0;
    if (!pcieLinkList)
    {
        error("Parsing of topology file failed : pcieLinkList is null");
        return;
    }

    // The elements in the structure that were being parsed from the obtained
    // files via write() and writeFromMemory() were written by IBM enterprise
    // host firmware which runs on big-endian format. The DMA engine in BMC
    // (aspeed-xdma device driver) which is responsible for exposing the data
    // shared by the host in the shared VGA memory has no idea of the structure
    // of the data it's copying from host. So it's up to the consumers of the
    // data to deal with the endianness once it has been copied to user space &
    // and as pldm is the first application layer that interprets the data
    // provided by host reading from the sysfs interface of xdma-engine & also
    // since pldm application runs on BMC which could be little/big endian, its
    // essential to swap the endianness to agreed big-endian format (htobe)
    // before using the data.

    numOfLinks = htobe16(pcieLinkList->numPcieLinkEntries);

    // To fetch the PCIe link from the topology data, moving the pointer
    // by 8 bytes based on the size of other elements of the structure
    struct pcieLinkEntry* singleEntryData =
        reinterpret_cast<struct pcieLinkEntry*>(
            reinterpret_cast<uint8_t*>(pcieLinkList) + 8);

    if (!singleEntryData)
    {
        error("Parsing of topology file failed : single_link is null");
        return;
    }

    // iterate over every pcie link and get the link specific attributes
    for ([[maybe_unused]] const auto& link :
         std::views::iota(0) | std::views::take(numOfLinks))
    {
        // get the link id
        auto linkId = htobe16(singleEntryData->linkId);

        // get parent link id
        auto parentLinkId = htobe16(singleEntryData->parentLinkId);

        // get link status
        auto linkStatus = singleEntryData->linkStatus;

        // get link type
        auto type = singleEntryData->linkType;
        if (type != pldm::responder::linkTypeData::Unknown)
        {
            linkTypeInfo[linkId] = type;
        }

        // get link speed
        auto linkSpeed = singleEntryData->linkSpeed;

        // get link width
        auto width = singleEntryData->linkWidth;

        auto singleEntryDataCharStream =
            reinterpret_cast<char*>(singleEntryData);

        // get the PCIe Host Bridge Location
        size_t pcieLocCodeSize = singleEntryData->pcieHostBridgeLocCodeSize;
        std::vector<char> pcieHostBridgeLocation(
            singleEntryDataCharStream +
                htobe16(singleEntryData->pcieHostBridgeLocCodeOff),
            singleEntryDataCharStream +
                htobe16(singleEntryData->pcieHostBridgeLocCodeOff) +
                static_cast<int>(pcieLocCodeSize));

        std::string pcieHostBridgeLocationCode(pcieHostBridgeLocation.begin(),
                                               pcieHostBridgeLocation.end());

        // get the local port - top location
        size_t localTopPortLocSize = singleEntryData->topLocalPortLocCodeSize;
        std::vector<char> localTopPortLocation(
            singleEntryDataCharStream +
                htobe16(singleEntryData->topLocalPortLocCodeOff),
            singleEntryDataCharStream +
                htobe16(singleEntryData->topLocalPortLocCodeOff) +
                static_cast<int>(localTopPortLocSize));
        std::string localTopPortLocationCode(localTopPortLocation.begin(),
                                             localTopPortLocation.end());

        // get the local port - bottom location
        size_t localBottomPortLocSize =
            singleEntryData->bottomLocalPortLocCodeSize;
        std::vector<char> localBottomPortLocation(
            singleEntryDataCharStream +
                htobe16(singleEntryData->bottomLocalPortLocCodeOff),
            singleEntryDataCharStream +
                htobe16(singleEntryData->bottomLocalPortLocCodeOff) +
                static_cast<int>(localBottomPortLocSize));
        std::string localBottomPortLocationCode(localBottomPortLocation.begin(),
                                                localBottomPortLocation.end());

        size_t remoteTopPortLocSize = singleEntryData->topRemotePortLocCodeSize;
        std::vector<char> remoteTopPortLocation(
            singleEntryDataCharStream +
                htobe16(singleEntryData->topRemotePortLocCodeOff),
            singleEntryDataCharStream +
                htobe16(singleEntryData->topRemotePortLocCodeOff) +
                static_cast<int>(remoteTopPortLocSize));
        std::string remoteTopPortLocationCode(remoteTopPortLocation.begin(),
                                              remoteTopPortLocation.end());

        // get the remote port - bottom location
        size_t remoteBottomLocSize =
            singleEntryData->bottomRemotePortLocCodeSize;
        std::vector<char> remoteBottomPortLocation(
            singleEntryDataCharStream +
                htobe16(singleEntryData->bottomRemotePortLocCodeOff),
            singleEntryDataCharStream +
                htobe16(singleEntryData->bottomRemotePortLocCodeOff) +
                static_cast<int>(remoteBottomLocSize));
        std::string remoteBottomPortLocationCode(
            remoteBottomPortLocation.begin(), remoteBottomPortLocation.end());

        struct slotLocCode* slotData = reinterpret_cast<struct slotLocCode*>(
            (reinterpret_cast<uint8_t*>(singleEntryData)) +
            htobe16(singleEntryData->slotLocCodesOffset));
        if (!slotData)
        {
            error("Parsing the topology file failed : slotData is null");
            return;
        }
        // get the Slot location code common part
        size_t numOfSlots = slotData->numSlotLocCodes;
        size_t slotLocCodeCompartSize = slotData->slotLocCodesCmnPrtSize;

        std::vector<char> slotLocation(
            reinterpret_cast<char*>(slotData->slotLocCodesCmnPrt),
            reinterpret_cast<char*>(slotData->slotLocCodesCmnPrt) +
                static_cast<int>(slotLocCodeCompartSize));
        std::string slotLocationCode(slotLocation.begin(), slotLocation.end());

        uint8_t* suffixData =
            reinterpret_cast<uint8_t*>(slotData) + slotLocationDataMemberSize +
            slotData->slotLocCodesCmnPrtSize;
        if (!suffixData)
        {
            error("slot location suffix data is nullptr");
            return;
        }

        // create the full slot location code by combining common part and
        // suffix part
        std::string slotSuffixLocationCode;
        std::vector<std::string> slotFinaLocationCode{};
        for ([[maybe_unused]] const auto& slot :
             std::views::iota(0) | std::views::take(numOfSlots))
        {
            struct slotLocCodeSuf* slotLocSufData =
                reinterpret_cast<struct slotLocCodeSuf*>(suffixData);
            if (!slotLocSufData)
            {
                error("slot location suffix data is nullptr");
                break;
            }

            size_t slotLocCodeSuffixSize = slotLocSufData->slotLocCodeSz;
            if (slotLocCodeSuffixSize > 0)
            {
                std::vector<char> slotSuffixLocation(
                    reinterpret_cast<char*>(slotLocSufData) + 1,
                    reinterpret_cast<char*>(slotLocSufData) + 1 +
                        static_cast<int>(slotLocCodeSuffixSize));
                std::string slotSuffLocationCode(slotSuffixLocation.begin(),
                                                 slotSuffixLocation.end());

                slotSuffixLocationCode = slotSuffLocationCode;
            }
            std::string slotFullLocationCode =
                slotLocationCode + slotSuffixLocationCode;

            slotFinaLocationCode.push_back(slotFullLocationCode);

            // move the pointer to next slot
            suffixData += sizeOfSuffixSizeDataMember + slotLocCodeSuffixSize;
        }

        // store the information into a map
        topologyInformation[linkId] = std::make_tuple(
            linkStateMap[linkStatus], type, linkSpeed, linkWidth[width],
            pcieHostBridgeLocationCode,
            std::make_pair(localTopPortLocationCode,
                           localBottomPortLocationCode),
            std::make_pair(remoteTopPortLocationCode,
                           remoteBottomPortLocationCode),
            slotFinaLocationCode, parentLinkId);

        // move the pointer to next link
        singleEntryData = reinterpret_cast<struct pcieLinkEntry*>(
            reinterpret_cast<uint8_t*>(singleEntryData) +
            htobe16(singleEntryData->entryLength));
    }
    // Need to call cable info at the end , because we dont want to parse
    // cable info without parsing the successful topology successfully
    // Having partial information is of no use.
    parseCableInfo();
}

void PCIeInfoHandler::parseCableInfo()
{
    int fd = open((fs::path(pciePath) / cableInfoFile).string().c_str(),
                  O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd == -1)
    {
        perror("CableInfo file not present");
        return;
    }
    pldm::responder::utils::CustomFD cableInfoFd(fd);
    struct stat sb;

    if (fstat(fd, &sb) == -1)
    {
        perror("Could not get cableinfo file size");
        return;
    }

    if (sb.st_size == 0)
    {
        error("Topology file Size is 0");
        return;
    }

    auto cableInfoCleanup = [sb](void* fileInMemory) {
        munmap(fileInMemory, sb.st_size);
    };

    void* fileInMemory =
        mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, cableInfoFd(), 0);

    if (MAP_FAILED == fileInMemory)
    {
        int rc = -errno;
        error("mmap on cable ifno file failed, RC={RC}", "RC", rc);
        return;
    }

    std::unique_ptr<void, decltype(cableInfoCleanup)> cablePtr(
        fileInMemory, cableInfoCleanup);

    auto cableList =
        reinterpret_cast<struct cableAttributesList*>(fileInMemory);

    // get number of cable links
    auto numOfCableLinks = htobe16(cableList->numOfCables);

    struct pcieLinkCableAttr* cableData =
        reinterpret_cast<struct pcieLinkCableAttr*>(
            (reinterpret_cast<uint8_t*>(cableList)) + 8);

    if (!cableData)
    {
        error("Cable info parsing failed , cableData = nullptr");
        return;
    }

    // iterate over each pci cable link
    for (const auto& cable :
         std::views::iota(0) | std::views::take(numOfCableLinks))
    {
        // get the link id
        auto linkId = htobe16(cableData->linkId);
        char* cableDataPtr = reinterpret_cast<char*>(cableData);

        std::string localPortLocCode(
            cableDataPtr + htobe16(cableData->hostPortLocationCodeOffset),
            cableData->hostPortLocationCodeSize);

        std::string ioSlotLocationCode(
            cableDataPtr +
                htobe16(cableData->ioEnclosurePortLocationCodeOffset),
            cableData->ioEnclosurePortLocationCodeSize);

        std::string cablePartNum(
            cableDataPtr + htobe16(cableData->cablePartNumberOffset),
            cableData->cablePartNumberSize);

        // cache the data into a map
        cableInformation[cable] = std::make_tuple(
            linkId, localPortLocCode, ioSlotLocationCode, cablePartNum,
            cableLengthMap[cableData->cableLength],
            cableTypeMap[cableData->cableType],
            cableStatusMap[cableData->cableStatus]);

        // move the cable data pointer
        cableData = reinterpret_cast<struct pcieLinkCableAttr*>(
            (reinterpret_cast<uint8_t*>(cableData)) +
            htobe16(cableData->entryLength));
    }
}

void PCIeInfoHandler::deleteTopologyFiles()
{
    if (receivedFiles.empty())
    {
        try
        {
            for (auto& path : fs::directory_iterator(pciePath))
            {
                fs::remove_all(path);
            }
        }
        catch (const fs::filesystem_error& err)
        {
            error("Topology file deletion failed {PCIE_PATH} : {ERR_EXCEP}",
                  "PCIE_PATH", pciePath, "ERR_EXCEP", err.what());
        }
    }
}

} // namespace responder
} // namespace pldm
