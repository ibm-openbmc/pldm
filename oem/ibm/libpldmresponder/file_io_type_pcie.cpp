#include "file_io_type_pcie.hpp"

#include "libpldm/base.h"
#include "oem/ibm/libpldm/file_io.h"

#include "common/utils.hpp"
#include "host-bmc/dbus/custom_dbus.hpp"
#include "host-bmc/dbus/serialize.hpp"

#include <stdint.h>

#include <iostream>
#include <ranges>

namespace pldm
{
namespace responder
{

static constexpr auto pciePath = "/var/lib/pldm/pcie-topology/";
constexpr auto topologyFile = "topology";
constexpr auto cableInfoFile = "cableinfo";
constexpr auto itemPCIeSlot = "xyz.openbmc_project.Inventory.Item.PCIeSlot";
constexpr auto itemPCIeDevice = "xyz.openbmc_project.Inventory.Item.PCIeDevice";
constexpr auto itemConnector = "xyz.openbmc_project.Inventory.Item.Connector";

namespace fs = std::filesystem;
std::unordered_map<uint16_t, bool> PCIeInfoHandler::receivedFiles;
std::unordered_map<linkId_t,
                   std::tuple<linkStatus_t, linkType_t, linkSpeed_t,
                              linkWidth_t, pcieHostBidgeloc_t, localport_t,
                              remoteport_t, io_slot_location_t, linkId_t>>
    PCIeInfoHandler::topologyInformation;
std::unordered_map<
    cable_link_no_t,
    std::tuple<linkId_t, local_port_loc_code_t, io_slot_location_code_t,
               cable_part_no_t, cable_length_t, cable_type_t, cable_status_t>>
    PCIeInfoHandler::cableInformation;
std::map<std::string,
         std::tuple<uint16_t, std::string, std::optional<std::string>>>
    PCIeInfoHandler::mexObjectMap;

std::vector<std::string> PCIeInfoHandler::cables;
std::vector<std::pair<linkId_t, linkId_t>> PCIeInfoHandler::needPostProcessing;
std::unordered_map<linkId_t, linkType_t> PCIeInfoHandler::linkTypeInfo;

PCIeInfoHandler::PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType) :
    FileHandler(fileHandle), infoType(fileType)
{
    deleteTopologyFiles();
    receivedFiles.emplace(infoType, false);
}
int PCIeInfoHandler::writeFromMemory(
    uint32_t offset, uint32_t length, uint64_t address,
    oem_platform::Handler* /*oemPlatformHandler*/)
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

    std::ofstream pcieData(infoFile, std::ios::out | std::ios::binary);
    if (!pcieData)
    {
        std::cerr << "PCIe Info file creation error " << std::endl;
        return PLDM_ERROR;
    }

    auto rc = transferFileData(infoFile, false, offset, length, address);
    if (rc != PLDM_SUCCESS)
    {
        std::cerr << "transferFileData failed with rc= " << rc << " \n";
        return rc;
    }

    return PLDM_SUCCESS;
}

int PCIeInfoHandler::write(const char* buffer, uint32_t, uint32_t& length,
                           oem_platform::Handler* /*oemPlatformHandler*/)
{
    fs::path infoFile(fs::path(pciePath) / topologyFile);
    if (infoType == PLDM_FILE_TYPE_CABLE_INFO)
    {
        infoFile = (fs::path(pciePath) / cableInfoFile);
    }

    std::ofstream pcieData(infoFile,
                           std::ios::out | std::ios::binary | std::ios::app);
    if (!pcieData)
    {
        std::cerr << "PCIe Info file creation error: " << infoFile << std::endl;
        return PLDM_ERROR;
    }

    if (buffer != nullptr)
    {
        pcieData.write(buffer, length);
    }
    pcieData.close();

    return PLDM_SUCCESS;
}

int PCIeInfoHandler::fileAck(uint8_t /*fileStatus*/)
{
    if (receivedFiles.find(infoType) == receivedFiles.end())
    {
        std::cerr << "Received FileAck for the file which is not received"
                  << std::endl;
    }
    receivedFiles[infoType] = true;

    if (receivedFiles.contains(PLDM_FILE_TYPE_CABLE_INFO) &&
        receivedFiles.contains(PLDM_FILE_TYPE_PCIE_TOPOLOGY))
    {
        if (receivedFiles[PLDM_FILE_TYPE_CABLE_INFO] &&
            receivedFiles[PLDM_FILE_TYPE_PCIE_TOPOLOGY])
        {
            receivedFiles.clear();

            // parse the topology blob and cache the information
            // for further processing
            parseTopologyData();

            getMexObjects();

            // delete the cable dbus as the cable are dynamic

            for (const auto& cable : cables)
            {
                std::cerr << "Removing cable object [ " << cable << " ]\n";
                pldm::dbus::CustomDBus::getCustomDBus().deleteObject(cable);
            }
            cables.clear();

            // set topology properties & host cable dbus objects
            setTopologyAttrsOnDbus();

            // even when we fail to parse /set the topology infromation on to
            // the dbus, we need to set the property back to false - to allow
            // the redfish/user to be able to ask the topology information again
            pldm::dbus::CustomDBus::getCustomDBus().updateTopologyProperty(
                false);

            // clear all the cached information
            clearTopologyInfo();
        }
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
        std::cerr << "Failed To set " << propertyName << "property"
                  << "and path :" << objPath << "ERROR=" << e.what()
                  << std::endl;
    }
}

void PCIeInfoHandler::getMexObjects()
{
    // get the in memory cached copy of mex objects
    auto savedObjs = pldm::serialize::Serialize::getSerialize().getSavedObjs();

    // Find the PCIE slots & PCIeAdapters & Connecters & their location codes
    std::set<uint16_t> neededEntityTypes = {PLDM_ENTITY_SLOT, PLDM_ENTITY_CARD,
                                            PLDM_ENTITY_CONNECTOR,
                                            PLDM_ENTITY_SYSTEM_CHASSIS};
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
        if (std::get<0>(obj) == entityType &&
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

std::string
    PCIeInfoHandler::getAdapterFromSlot(const std::string& mexSlotObject)
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
    const uint32_t& linkId, const std::string& linkStatus, uint8_t linkSpeed,
    int64_t linkWidth, bool isHostedByPLDM)
{
    if (!slotAndAdapter.first.empty())
    {
        if (linkType == Primary)
        {
            // we got the slot dbus object, set linkid, linkstatus on it
            // set the busid on the respective slots
            setProperty(slotAndAdapter.first, "BusId", linkId, itemPCIeSlot,
                        "uint32_t");

            // set the link status on the respective slots
            setProperty(slotAndAdapter.first, "LinkStatus", linkStatus,
                        itemPCIeSlot, "string");

            std::filesystem::path slot(slotAndAdapter.first);

            std::cerr << "Primary Link, "
                      << " BusId - [" << std::hex << std::showbase << linkId
                      << "] LinkStatus - [" << linkStatus << "] on [ "
                      << slot.filename() << "] \n";
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

            std::cerr << "Secondary Link, "
                      << " BusId - [ " << std::hex << std::showbase << linkId
                      << " ] LinkStatus - [" << linkStatus << "] on [ "
                      << printSlot << "] \n";
        }
    }
    if (!slotAndAdapter.second.empty())
    {
        if (linkType == Primary)
        {
            // we got the adapter dbus object, set linkspeed & linkwidth on it
            setProperty(slotAndAdapter.second, "GenerationInUse",
                        link_speed[linkSpeed], itemPCIeDevice, "string");

            // set link width
            setProperty(slotAndAdapter.second, "LanesInUse", linkWidth,
                        itemPCIeDevice, "int64_t");
            std::filesystem::path adapter(slotAndAdapter.second);

            std::cerr << "Primary Link, "
                      << " GenerationInUse - [" << link_speed[linkSpeed]
                      << "] LanesInUse - [" << linkWidth << "] on [ "
                      << adapter.filename() << "]\n";
        }
        else
        {
            if (isHostedByPLDM)
            {
                // std::cerr << "secondary link not empty adapter \n";
                //  its a secondary link so update the link speed on mex adapter
                if (linkStatus ==
                        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Open" ||
                    linkStatus ==
                        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unknown")
                {
                    // There is no adapter plugged in so set the adapter present
                    // property to false
                    pldm::dbus::CustomDBus::getCustomDBus()
                        .updateItemPresentStatus(slotAndAdapter.second, false);
                }
                else if (
                    linkStatus ==
                    "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Operational")
                {
                    // Thre is adapter plugged in , its powered on and is
                    // operational, so change the present property of the
                    // adapter to true
                    pldm::dbus::CustomDBus::getCustomDBus()
                        .updateItemPresentStatus(slotAndAdapter.second, true);
                }
                pldm::dbus::CustomDBus::getCustomDBus().setPCIeDeviceProps(
                    slotAndAdapter.second, linkWidth, link_speed[linkSpeed]);
            }
            else
            {
                // we got the adapter dbus object, set linkspeed & linkwidth on
                // it
                setProperty(slotAndAdapter.second, "GenerationInUse",
                            link_speed[linkSpeed], itemPCIeDevice, "string");

                // set link width
                setProperty(slotAndAdapter.second, "LanesInUse", linkWidth,
                            itemPCIeDevice, "int64_t");
            }

            std::filesystem::path adapter(slotAndAdapter.second);
            std::filesystem::path printAdapter =
                adapter.parent_path().parent_path().filename();
            printAdapter /= adapter.parent_path().filename();
            printAdapter /= adapter.filename();

            std::cerr << "Secondary Link, GenerationInUse - ["
                      << link_speed[linkSpeed] << "] LanesInUse - ["
                      << linkWidth << "] on [ " << printAdapter << "]\n";
        }
    }
}
void PCIeInfoHandler::parsePrimaryLink(
    uint8_t linkType, const io_slot_location_t& ioSlotLocationCode,
    const localport_t& localPortLocation, const uint32_t& linkId,
    const std::string& linkStatus, uint8_t linkSpeed, int64_t linkWidth,
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
                                            linkStatus, linkSpeed, linkWidth,
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
                    linkId, linkStatus, linkSpeed, linkWidth, false);
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
            linkStatus, linkSpeed, linkWidth, false);
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
                                            linkStatus, linkSpeed, linkWidth,
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
                    linkId, linkStatus, linkSpeed, linkWidth, false);
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
    uint8_t linkType, const io_slot_location_t& ioSlotLocationCode,
    const localport_t& /*localPortLocation*/,
    const remoteport_t& remotePortLocation, const uint32_t& linkId,
    const std::string& linkStatus, uint8_t linkSpeed, int64_t linkWidth)
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
                    linkId, linkStatus, linkSpeed, linkWidth, true);
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
                linkId, linkStatus, linkSpeed, linkWidth, false);
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

        // set topology info on both the slot and adapter object
        setTopologyOnSlotAndAdapter(linkType, mexSlotandAdapter, linkId,
                                    linkStatus, linkSpeed, linkWidth, true);
    }
}

void PCIeInfoHandler::parseSpeciallink(linkId_t linkId,
                                       linkId_t /*parentLinkId*/)
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
                std::cerr << "Special processing for link - [ " << linkId
                          << " ] \n";
                setTopologyOnSlotAndAdapter(
                    std::get<1>(linkInfo), slotAndAdapter, linkId,
                    std::get<0>(linkInfo), std::get<2>(linkInfo),
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
            case Primary:
                parsePrimaryLink(std::get<1>(info), std::get<7>(info),
                                 std::get<5>(info), static_cast<uint32_t>(link),
                                 std::get<0>(info), std::get<2>(info),
                                 std::get<3>(info), std::get<8>(info));
                break;
            case Secondary:
                parseSecondaryLink(
                    std::get<1>(info), std::get<7>(info), std::get<5>(info),
                    std::get<6>(info), static_cast<uint32_t>(link),
                    std::get<0>(info), std::get<2>(info), std::get<3>(info));
                break;
            case Unknown:
                std::cerr << "link type is unkown : " << (unsigned)link
                          << std::endl;
                switch (linkTypeInfo[link])
                {
                    case Primary:
                        parsePrimaryLink(linkTypeInfo[link], std::get<7>(info),
                                         std::get<5>(info),
                                         static_cast<uint32_t>(link),
                                         std::get<0>(info), std::get<2>(info),
                                         std::get<3>(info), std::get<8>(info));
                        break;
                    case Secondary:
                        parseSecondaryLink(
                            linkTypeInfo[link], std::get<7>(info),
                            std::get<5>(info), std::get<6>(info),
                            static_cast<uint32_t>(link), std::get<0>(info),
                            std::get<2>(info), std::get<3>(info));
                        break;
                }
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
        pldm::dbus::CustomDBus::getCustomDBus().updateItemPresentStatus(
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

        std::cerr << "Hosted Cable [ " << cable_no << " ] Length - [ "
                  << std::get<4>(info) << " ] Type - [ " << std::get<5>(info)
                  << " ] Status - [ " << std::get<6>(info) << " ] PN - [ "
                  << std::get<3>(info) << " ]\n";
        std::cerr << "Hosted Cable [ " << cable_no << " ] UPConnector - [ "
                  << upstreamInformation << " ] DNConnector - [ "
                  << downstreamInformation << " ] DChassis - [ "
                  << downstreamChassis << " ]\n";

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
    pldm::utils::CustomFD topologyFd(fd);
    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        perror("Could not get topology file size");
        return;
    }

    auto topologyCleanup = [sb](void* file_in_memory) {
        munmap(file_in_memory, sb.st_size);
    };

    // memory map the topology file into pldm memory
    void* file_in_memory =
        mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, topologyFd(), 0);
    if (MAP_FAILED == file_in_memory)
    {
        int rc = -errno;
        std::cerr << "mmap on topology file failed, RC=" << rc << std::endl;
        return;
    }

    std::unique_ptr<void, decltype(topologyCleanup)> topologyPtr(
        file_in_memory, topologyCleanup);

    auto pcie_link_list =
        reinterpret_cast<struct topology_blob*>(file_in_memory);
    uint16_t no_of_links = 0;
    if (pcie_link_list != nullptr)
    {
        no_of_links = htobe16(pcie_link_list->num_pcie_link_entries);
    }
    else
    {
        std::cerr
            << "Parsing of topology file failed : pcie_link_list is null\n";
        return;
    }

    struct pcie_link_entry* single_entry_data =
        (struct pcie_link_entry*)(((uint8_t*)pcie_link_list) + 8);

    if (single_entry_data == nullptr)
    {
        std::cerr << "Parsing of topology file failed : single_link is null \n";
        return;
    }

    // iterate over every pcie link and get the link specific attributes
    for ([[maybe_unused]] const auto& link :
         std::views::iota(0) | std::views::take(no_of_links))
    {

        // get the link id
        auto linkid = htobe16(single_entry_data->link_id);

        // get parent link id
        auto parentLinkId = htobe16(single_entry_data->parent_link_id);

        // get link status
        auto linkStatus = single_entry_data->link_status;

        // get link type
        auto linkType = single_entry_data->link_type;
        if (linkType != Unknown)
        {
            linkTypeInfo[linkid] = linkType;
        }

        // get link speed
        auto linkSpeed = single_entry_data->link_speed;

        // get link width
        auto linkWidth = single_entry_data->link_width;

        // get the PCIe Host Bridge Location
        size_t pcie_loc_code_size =
            single_entry_data->PCIehostBridgeLocCodeSize;
        std::vector<char> pcie_host_bridge_location(
            (char*)single_entry_data +
                htobe16(single_entry_data->PCIehostBridgeLocCodeOff),
            (char*)single_entry_data +
                htobe16(single_entry_data->PCIehostBridgeLocCodeOff) +
                (unsigned)pcie_loc_code_size);
        std::string pcie_host_bridge_location_code(
            pcie_host_bridge_location.begin(), pcie_host_bridge_location.end());

        // get the local port - top location
        size_t local_top_port_loc_size =
            single_entry_data->TopLocalPortLocCodeSize;
        std::vector<char> local_top_port_location(
            (char*)single_entry_data +
                htobe16(single_entry_data->TopLocalPortLocCodeOff),
            (char*)single_entry_data +
                htobe16(single_entry_data->TopLocalPortLocCodeOff) +
                (int)local_top_port_loc_size);
        std::string local_top_port_location_code(
            local_top_port_location.begin(), local_top_port_location.end());

        // get the local port - bottom location
        size_t local_bottom_port_loc_size =
            single_entry_data->BottomLocalPortLocCodeSize;
        std::vector<char> local_bottom_port_location(
            (char*)single_entry_data +
                htobe16(single_entry_data->BottomLocalPortLocCodeOff),
            (char*)single_entry_data +
                htobe16(single_entry_data->BottomLocalPortLocCodeOff) +
                (int)local_bottom_port_loc_size);
        std::string local_bottom_port_location_code(
            local_bottom_port_location.begin(),
            local_bottom_port_location.end());

        // get the remote port - top location
        size_t remote_top_port_loc_size =
            single_entry_data->TopRemotePortLocCodeSize;
        std::vector<char> remote_top_port_location(
            (char*)single_entry_data +
                htobe16(single_entry_data->TopRemotePortLocCodeOff),
            (char*)single_entry_data +
                htobe16(single_entry_data->TopRemotePortLocCodeOff) +
                (int)remote_top_port_loc_size);
        std::string remote_top_port_location_code(
            remote_top_port_location.begin(), remote_top_port_location.end());

        // get the remote port - bottom location
        size_t remote_bottom_loc_size =
            single_entry_data->BottomRemotePortLocCodeSize;
        std::vector<char> remote_bottom_port_location(
            (char*)single_entry_data +
                htobe16(single_entry_data->BottomRemotePortLocCodeOff),
            (char*)single_entry_data +
                htobe16(single_entry_data->BottomRemotePortLocCodeOff) +
                (int)remote_bottom_loc_size);
        std::string remote_bottom_port_location_code(
            remote_bottom_port_location.begin(),
            remote_bottom_port_location.end());

        struct SlotLocCode_t* slot_data =
            (struct SlotLocCode_t*)(((uint8_t*)single_entry_data) +
                                    htobe16(single_entry_data
                                                ->slot_loc_codes_offset));

        if (slot_data == nullptr)
        {
            std::cerr
                << "Parsing the topology file failed : slot_data is null \n";
            return;
        }
        // get the Slot location code common part
        size_t no_of_slots = slot_data->numSlotLocCodes;
        size_t slot_loccode_compart_size = slot_data->slotLocCodesCmnPrtSize;
        std::vector<char> slot_location((char*)slot_data->slotLocCodesCmnPrt,
                                        (char*)slot_data->slotLocCodesCmnPrt +
                                            (int)slot_loccode_compart_size);
        std::string slot_location_code(slot_location.begin(),
                                       slot_location.end());

        struct SlotLocCodeSuf_t* slot_loc_suf_data =
            (struct SlotLocCodeSuf_t*)(((uint8_t*)slot_data) + 2 +
                                       slot_data->slotLocCodesCmnPrtSize);
        if (slot_loc_suf_data == nullptr)
        {
            std::cerr << "slot location suffix data is nullptr \n";
            return;
        }

        // create the full slot location code by combining common part and
        // suffix part
        std::string slot_suffix_location_code;
        std::vector<std::string> slot_final_location_code{};
        for ([[maybe_unused]] const auto& slot :
             std::views::iota(0) | std::views::take(no_of_slots))
        {
            size_t slot_loccode_suffix_size = slot_loc_suf_data->slotLocCodeSz;
            if (slot_loccode_suffix_size > 0)
            {
                std::vector<char> slot_suffix_location(
                    (char*)slot_loc_suf_data + 1,
                    (char*)slot_loc_suf_data + 1 +
                        (int)slot_loccode_suffix_size);
                std::string slot_suff_location_code(
                    slot_suffix_location.begin(), slot_suffix_location.end());

                slot_suffix_location_code = slot_suff_location_code;
            }
            std::string slot_full_location_code =
                slot_location_code + slot_suffix_location_code;
            slot_final_location_code.push_back(slot_full_location_code);

            // move the pointer to next slot
            slot_loc_suf_data += 2;
        }

        // store the information into a map
        topologyInformation[linkid] = std::make_tuple(
            link_state_map[linkStatus], linkType, linkSpeed,
            link_width[linkWidth], pcie_host_bridge_location_code,
            std::make_pair(local_top_port_location_code,
                           local_bottom_port_location_code),
            std::make_pair(remote_top_port_location_code,
                           remote_bottom_port_location_code),
            slot_final_location_code, parentLinkId);

        // move the pointer to next link
        single_entry_data =
            (struct pcie_link_entry*)((uint8_t*)single_entry_data +
                                      htons(single_entry_data->entry_length));
    }
    // Need to call cable info at the end , because we dont want to parse
    // cable info without parsing the successfull topology successfully
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
    pldm::utils::CustomFD cableInfoFd(fd);
    struct stat sb;

    if (fstat(fd, &sb) == -1)
    {
        perror("Could not get cableinfo file size");
        return;
    }

    auto cableInfoCleanup = [sb](void* file_in_memory) {
        munmap(file_in_memory, sb.st_size);
    };

    void* file_in_memory =
        mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, cableInfoFd(), 0);

    if (MAP_FAILED == file_in_memory)
    {
        int rc = -errno;
        std::cerr << "mmap on cable ifno file failed, RC=" << rc << std::endl;
        return;
    }

    std::unique_ptr<void, decltype(cableInfoCleanup)> cablePtr(
        file_in_memory, cableInfoCleanup);

    auto cable_list =
        reinterpret_cast<struct cable_attributes_list*>(file_in_memory);

    // get number of cable links
    auto no_of_cable_links = htobe16(cable_list->no_of_cables);

    struct pcilinkcableattr_t* cable_data =
        (struct pcilinkcableattr_t*)(((uint8_t*)cable_list) + 8);

    if (cable_data == nullptr)
    {
        std::cerr << "Cable info parsing failed , cable_data = nullptr \n";
        return;
    }

    // iterate over each pci cable link
    for (const auto& cable :
         std::views::iota(0) | std::views::take(no_of_cable_links))
    {
        // get the link id
        auto linkid = htobe16(cable_data->link_id);

        std::string local_port_loc_code(
            (char*)cable_data +
                htobe16(cable_data->host_port_location_code_offset),
            cable_data->host_port_location_code_size);

        std::string io_slot_location_code(
            (char*)cable_data +
                htobe16(cable_data->io_enclosure_port_location_code_offset),
            cable_data->io_enclosure_port_location_code_size);

        std::string cable_part_num(
            (char*)cable_data + htobe16(cable_data->cable_part_number_offset),
            cable_data->cable_part_number_size);

        // cache the data into a map
        cableInformation[cable] = std::make_tuple(
            linkid, local_port_loc_code, io_slot_location_code, cable_part_num,
            cable_length_map[cable_data->cable_length],
            cable_type_map[cable_data->cable_type],
            cable_status_map[cable_data->cable_status]);
        // move the cable data pointer

        cable_data =
            (struct pcilinkcableattr_t*)(((uint8_t*)cable_data) +
                                         ntohs(cable_data->entry_length));
    }
}

int PCIeInfoHandler::newFileAvailable(uint64_t)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::readIntoMemory(
    uint32_t, uint32_t, uint64_t, oem_platform::Handler* /*oemPlatformHandler*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::read(uint32_t, uint32_t&, Response&,
                          oem_platform::Handler* /*oemPlatformHandler*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::fileAckWithMetaData(uint8_t /*fileStatus*/,
                                         uint32_t /*metaDataValue1*/,
                                         uint32_t /*metaDataValue2*/,
                                         uint32_t /*metaDataValue3*/,
                                         uint32_t /*metaDataValue4*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
}

int PCIeInfoHandler::newFileAvailableWithMetaData(uint64_t /*length*/,
                                                  uint32_t /*metaDataValue1*/,
                                                  uint32_t /*metaDataValue2*/,
                                                  uint32_t /*metaDataValue3*/,
                                                  uint32_t /*metaDataValue4*/)
{
    return PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
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
            std::cerr << "Topology file deletion failed " << pciePath << " : "
                      << err.what() << "\n";
        }
    }
}

} // namespace responder
} // namespace pldm
