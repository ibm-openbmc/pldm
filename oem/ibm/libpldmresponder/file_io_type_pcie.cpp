#include "file_io_type_pcie.hpp"

#include "libpldm/base.h"
#include "oem/ibm/libpldm/file_io.h"

#include "common/utils.hpp"
#include "host-bmc/dbus/custom_dbus.hpp"

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

namespace fs = std::filesystem;
std::unordered_map<uint16_t, bool> PCIeInfoHandler::receivedFiles;
std::unordered_map<linkId_t,
                   std::tuple<linkStatus_t, linkType_t, linkSpeed_t,
                              linkWidth_t, pcieHostBidgeloc_t, localport_t,
                              remoteport_t, io_slot_location_t>>
    PCIeInfoHandler::topologyInformation;
std::unordered_map<
    cable_link_no_t,
    std::tuple<linkId_t, local_port_loc_code_t, io_slot_location_code_t,
               cable_part_no_t, cable_length_t, cable_type_t, cable_status_t>>
    PCIeInfoHandler::cableInformation;

PCIeInfoHandler::PCIeInfoHandler(uint32_t fileHandle, uint16_t fileType) :
    FileHandler(fileHandle), infoType(fileType)
{
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

void PCIeInfoHandler::clearTopologyInfo()
{
    std::cerr << "Topology information :\n";
    for (const auto& [link, info] : topologyInformation)
    {
        std::cerr << "linkid :" << (unsigned)link << std::endl;
        std::cerr << "link status : " << std::get<0>(info) << std::endl;
        std::cerr << "link type :" << std::get<1>(info) << std::endl;
        std::cerr << "link speed : " << std::get<2>(info) << std::endl;
        std::cerr << "link wdith : " << std::get<3>(info) << std::endl;
        std::cerr << "pcie host bridge loc code : " << std::get<4>(info)
                  << std::endl;
        std::cerr << "local port top loc code : " << std::get<5>(info).first
                  << std::endl;
        std::cerr << "local port bottom loc code : " << std::get<5>(info).second
                  << std::endl;
        std::cerr << "remote port top loc code : " << std::get<6>(info).first
                  << std::endl;
        std::cerr << "remote port bottom loc code : "
                  << std::get<6>(info).second << std::endl;
        for (const auto& slot : std::get<7>(info))
        {
            std::cerr << "io slot location code : " << slot << std::endl;
        }
    }
    topologyInformation.clear();

    std::cerr << "Cable iformation : \n";
    for (const auto& [cable_no, info] : cableInformation)
    {
        std::cerr << "cable no : " << (unsigned)cable_no << std::endl;
        std::cerr << "link id : " << (unsigned)std::get<0>(info) << std::endl;
        std::cerr << "local port loc code : " << std::get<1>(info) << std::endl;
        std::cerr << "io slot loc code : " << std::get<2>(info) << std::endl;
        std::cerr << "cable part no : " << std::get<3>(info) << std::endl;
        std::cerr << "cable length : " << std::get<4>(info) << std::endl;
        std::cerr << "cable type : " << std::get<5>(info) << std::endl;
        std::cerr << "cable status :" << std::get<6>(info) << std::endl;
    }
    cableInformation.clear();
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

        // get link status
        auto linkStatus = single_entry_data->link_status;

        // get link type
        auto linkType = single_entry_data->link_type;

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
        topologyInformation[linkid] =
            std::make_tuple(link_state_map[linkStatus], link_type[linkType],
                            link_speed[linkSpeed], link_width[linkWidth],
                            pcie_host_bridge_location_code,
                            std::make_pair(local_top_port_location_code,
                                           local_bottom_port_location_code),
                            std::make_pair(remote_top_port_location_code,
                                           remote_bottom_port_location_code),
                            slot_final_location_code);

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
    uint32_t, uint32_t&, uint64_t,
    oem_platform::Handler* /*oemPlatformHandler*/)
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

} // namespace responder
} // namespace pldm
