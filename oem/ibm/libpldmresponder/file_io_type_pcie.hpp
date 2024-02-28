#pragma once

#include "file_io_by_type.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <unordered_map>

namespace pldm
{
namespace responder
{

/* Topology enum definitions*/

static std::map<uint8_t, std::string> link_state_map{
    {0x00, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Operational"},
    {0x01, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Degraded"},
    {0x02, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0x03, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0x04, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Failed"},
    {0x05, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Open"},
    {0x06, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Inactive"},
    {0x07, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unused"},
    {0xFF, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Status.Unknown"}};

enum link_type
{
    Primary = 0x0,
    Secondary = 0x1,
    OpenCAPI = 0x2,
    Unknown = 0xFF
};

static std::map<uint8_t, std::string> link_speed{
    {0x00, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen1"},
    {0x01, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen2"},
    {0x02, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen3"},
    {0x03, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen4"},
    {0x04, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen5"},
    {0x10,
     "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Unknown"}, // OC25
                                                                         // is
                                                                         // not
                                                                         // supported
                                                                         // on
                                                                         // BMC
    {0xFF, "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Unknown"}};

static std::map<uint8_t, int64_t> link_width{{0x01, 1}, {0x02, 2},  {0x04, 4},
                                             {0x08, 8}, {0x10, 16}, {0xFF, -1},
                                             {0x00, 0}};

struct SlotLocCode_t
{
    uint8_t numSlotLocCodes;
    uint8_t slotLocCodesCmnPrtSize;
    uint8_t slotLocCodesCmnPrt[1];
} __attribute__((packed));

struct SlotLocCodeSuf_t
{
    uint8_t slotLocCodeSz;
    uint8_t slotLocCodeSuf[1];
} __attribute__((packed));

struct pcie_link_entry
{
    uint16_t entry_length;
    uint8_t version;
    uint8_t reserved_1;
    uint16_t link_id;
    uint16_t parent_link_id;
    uint32_t link_drc_index;
    uint32_t hub_drc_index;
    uint8_t link_status;
    uint8_t link_type;
    uint16_t reserved_2;
    uint8_t link_speed;
    uint8_t link_width;
    uint8_t PCIehostBridgeLocCodeSize;
    uint16_t PCIehostBridgeLocCodeOff;
    uint8_t TopLocalPortLocCodeSize;
    uint16_t TopLocalPortLocCodeOff;
    uint8_t BottomLocalPortLocCodeSize;
    uint16_t BottomLocalPortLocCodeOff;
    uint8_t TopRemotePortLocCodeSize;
    uint16_t TopRemotePortLocCodeOff;
    uint8_t BottomRemotePortLocCodeSize;
    uint16_t BottomRemotePortLocCodeOff;
    uint16_t slot_loc_codes_offset;
    uint8_t pci_link_entry_loc_code[1];
} __attribute__((packed));

struct topology_blob
{
    uint32_t total_data_size;
    uint16_t num_pcie_link_entries;
    uint16_t reserved;
    pcie_link_entry pci_link_entry[1];
} __attribute__((packed));

using linkId_t = uint16_t;
using linkStatus_t = std::string;
using linkType_t = uint8_t;
using linkSpeed_t = uint8_t;
using linkWidth_t = int64_t;
using pcieHostBidgeloc_t = std::string;
using localport_top_t = std::string;
using localport_bot_t = std::string;
using localport_t = std::pair<localport_top_t, localport_bot_t>;
using remoteport_top_t = std::string;
using remoteport_bot_t = std::string;
using remoteport_t = std::pair<remoteport_top_t, remoteport_bot_t>;
using io_slot_location_t = std::vector<std::string>;

/* Cable Attributes Info */

static std::map<uint8_t, double> cable_length_map{
    {0x00, 0},  {0x01, 2},  {0x02, 3},
    {0x03, 10}, {0x04, 20}, {0xFF, std::numeric_limits<double>::quiet_NaN()}};

static std::map<uint8_t, std::string> cable_type_map{
    {0x00, "optical"}, {0x01, "copper"}, {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> cable_status_map{
    {0x00, "xyz.openbmc_project.Inventory.Item.Cable.Status.Inactive"},
    {0x01, "xyz.openbmc_project.Inventory.Item.Cable.Status.Running"},
    {0x02, "xyz.openbmc_project.Inventory.Item.Cable.Status.PoweredOff"},
    {0xFF, "xyz.openbmc_project.Inventory.Item.Cable.Status.Unknown"}};

using cable_link_no_t = unsigned short int;
using local_port_loc_code_t = std::string;
using io_slot_location_code_t = std::string;
using cable_part_no_t = std::string;
using cable_length_t = double;
using cable_type_t = std::string;
using cable_status_t = std::string;

struct pcilinkcableattr_t
{
    uint16_t entry_length;
    uint8_t version;
    uint8_t reserved_1;
    uint16_t link_id;
    uint16_t reserved_2;
    uint32_t link_drc_index;
    uint8_t cable_length;
    uint8_t cable_type;
    uint8_t cable_status;
    uint8_t host_port_location_code_size;
    uint8_t io_enclosure_port_location_code_size;
    uint8_t cable_part_number_size;
    uint16_t host_port_location_code_offset;
    uint16_t io_enclosure_port_location_code_offset;
    uint16_t cable_part_number_offset;
    uint8_t cable_attr_loc_code[1];
};

struct cable_attributes_list
{
    uint32_t length_of_response;
    uint16_t no_of_cables;
    uint16_t reserved;
    pcilinkcableattr_t pci_link_cable_attr[1];
};

/** @class PCIeInfoHandler
 *
 *  @brief Inherits and implements FileHandler. This class is used
 *  handle the pcie topology file and cable information from host to the bmc
 */
class PCIeInfoHandler : public FileHandler
{
  public:
    /** @brief PCIeInfoHandler constructor
     */
    PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType);

    virtual int writeFromMemory(uint32_t offset, uint32_t length,
                                uint64_t address,
                                oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int readIntoMemory(uint32_t offset, uint32_t& length,
                               uint64_t address,
                               oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int read(uint32_t offset, uint32_t& length, Response& response,
                     oem_platform::Handler* /*oemPlatformHandler*/);

    virtual int write(const char* buffer, uint32_t offset, uint32_t& length,
                      oem_platform::Handler* /*oemPlatformHandler*/,
                      struct fileack_status_metadata& /*metaDataObj*/);

    virtual int newFileAvailable(uint64_t length);

    virtual int fileAck(uint8_t fileStatus);

    virtual int fileAckWithMetaData(uint8_t /*fileStatus*/,
                                    uint32_t metaDataValue1,
                                    uint32_t metaDataValue2,
                                    uint32_t /*metaDataValue3*/,
                                    uint32_t /*metaDataValue4*/);

    virtual int newFileAvailableWithMetaData(uint64_t length,
                                             uint32_t metaDataValue1,
                                             uint32_t /*metaDataValue2*/,
                                             uint32_t /*metaDataValue3*/,
                                             uint32_t /*metaDataValue4*/);

    virtual void postWriteAction(
        const uint16_t /*fileType*/, const uint32_t /*fileHandle*/,
        const struct fileack_status_metadata& /*metaDataObj*/){};

    /** @brief method to parse the pcie topology information */
    virtual void parseTopologyData();

    /** @brief method to parse the cable information */
    virtual void parseCableInfo();

    /** @brief method to clear the topology cache */
    virtual void clearTopologyInfo();

    virtual void setTopologyAttrsOnDbus();

    virtual void getMexObjects();

    virtual void parsePrimaryLink(uint8_t linkType,
                                  const io_slot_location_t& ioSlotLocationCode,
                                  const localport_t& localPortLocation,
                                  const uint32_t& linkId,
                                  const std::string& linkStatus,
                                  uint8_t linkSpeed, int64_t linkWidth,
                                  uint8_t parentLinkId);
    virtual void parseSecondaryLink(
        uint8_t linkType, const io_slot_location_t& ioSlotLocationCode,
        const localport_t& localPortLocation,
        const remoteport_t& remotePortLocation, const uint32_t& linkId,
        const std::string& linkStatus, uint8_t linkSpeed, int64_t linkWidth);
    virtual void setTopologyOnSlotAndAdapter(
        uint8_t linkType,
        const std::pair<std::string, std::string>& slotAndAdapter,
        const uint32_t& linkId, const std::string& linkStatus,
        uint8_t linkSpeed, int64_t linkWidth, bool isHostedByPLDM);

    virtual void setProperty(const std::string& objPath,
                             const std::string& propertyName,
                             const pldm::utils::PropertyValue& propertyValue,
                             const std::string& interfaceName,
                             const std::string& propertyType);
    virtual std::string
        getMexObjectFromLocationCode(const std::string& locationCode,
                                     uint16_t entityType);
    virtual std::string getAdapterFromSlot(const std::string& mexSlotObject);

    virtual std::pair<std::string, std::string>
        getMexSlotandAdapter(const std::filesystem::path& connector);

    virtual std::string
        getDownStreamChassis(const std::string& slotOrConnecterPath);
    virtual void parseSpeciallink(linkId_t linkId, linkId_t parentLinkId);

    /** @brief PCIeInfoHandler destructor
     */
    ~PCIeInfoHandler() {}

  private:
    uint16_t infoType; //!< type of the information
    static std::unordered_map<uint16_t, bool> receivedFiles;
    static std::unordered_map<
        linkId_t, std::tuple<linkStatus_t, linkType_t, linkSpeed_t, linkWidth_t,
                             pcieHostBidgeloc_t, localport_t, remoteport_t,
                             io_slot_location_t, linkId_t>>
        topologyInformation;
    static std::unordered_map<
        cable_link_no_t,
        std::tuple<linkId_t, local_port_loc_code_t, io_slot_location_code_t,
                   cable_part_no_t, cable_length_t, cable_type_t,
                   cable_status_t>>
        cableInformation;
    static std::map<std::string, std::tuple<uint16_t, std::string,
                                            std::optional<std::string>>>
        mexObjectMap;
    static std::vector<std::string> cables;
    static std::vector<std::pair<linkId_t, linkId_t>> needPostProcessing;
    static std::unordered_map<linkId_t, linkType_t> linkTypeInfo;

    /** @brief Deletes the topology and cable data
     *
     *  @param[return] void
     */
    void deleteTopologyFiles();
};

} // namespace responder
} // namespace pldm
