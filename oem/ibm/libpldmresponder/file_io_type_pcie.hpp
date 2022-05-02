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
    {0x00, "Operational"}, {0x01, "Degraded"}, {0x02, "Unused1"},
    {0x03, "Unused2"},     {0x04, "Failed"},   {0x05, "Open"},
    {0x06, "Inactive"},    {0x07, "Unused3"},  {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> link_type{{0x00, "Primary"},
                                                {0x01, "Secondary"},
                                                {0x02, "OpenCAPI"},
                                                {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> link_speed{
    {0x00, "Gen1"}, {0x01, "Gen2"}, {0x02, "Gen3"},   {0x03, "Gen4"},
    {0x04, "Gen5"}, {0x10, "OC25"}, {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> link_width{
    {0x01, "x1"}, {0x02, "x2"},  {0x04, "x4"},
    {0x08, "x8"}, {0x10, "x16"}, {0xFF, "Unknown"}};

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
using linkType_t = std::string;
using linkSpeed_t = std::string;
using linkWidth_t = std::string;
using pcieHostBidgeloc_t = std::string;
using localport_top_t = std::string;
using localport_bot_t = std::string;
using localport_t = std::pair<localport_top_t, localport_bot_t>;
using remoteport_top_t = std::string;
using remoteport_bot_t = std::string;
using remoteport_t = std::pair<remoteport_top_t, remoteport_bot_t>;
using io_slot_location_t = std::vector<std::string>;

/* Cable Attributes Info */

static std::map<uint8_t, std::string> cable_length_map{
    {0x00, "0m"},  {0x01, "2m"},  {0x02, "3m"},
    {0x03, "10m"}, {0x04, "20m"}, {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> cable_type_map{
    {0x00, "optical"}, {0x01, "copper"}, {0xFF, "Unknown"}};

static std::map<uint8_t, std::string> cable_status_map{{0x00, "inactive"},
                                                       {0x01, "running"},
                                                       {0x02, "powered off"},
                                                       {0xFF, "Unknown"}};

using cable_link_no_t = unsigned short int;
using local_port_loc_code_t = std::string;
using io_slot_location_code_t = std::string;
using cable_part_no_t = std::string;
using cable_length_t = std::string;
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
                      oem_platform::Handler* /*oemPlatformHandler*/);

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

    /** @brief method to parse the pcie topology information */
    virtual void parseTopologyData();

    /** @brief method to parse the cable information */
    virtual void parseCableInfo();

    /** @brief method to clear the topology cache */
    virtual void clearTopologyInfo();

    /** @brief PCIeInfoHandler destructor
     */
    ~PCIeInfoHandler()
    {}

  private:
    uint16_t infoType; //!< type of the information
    static std::unordered_map<uint16_t, bool> receivedFiles;
    static std::unordered_map<
        linkId_t, std::tuple<linkStatus_t, linkType_t, linkSpeed_t, linkWidth_t,
                             pcieHostBidgeloc_t, localport_t, remoteport_t,
                             io_slot_location_t>>
        topologyInformation;
    static std::unordered_map<
        cable_link_no_t,
        std::tuple<linkId_t, local_port_loc_code_t, io_slot_location_code_t,
                   cable_part_no_t, cable_length_t, cable_type_t,
                   cable_status_t>>
        cableInformation;
};

} // namespace responder
} // namespace pldm
