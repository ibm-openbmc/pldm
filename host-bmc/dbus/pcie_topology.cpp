#include "pcie_topology.hpp"

#include "libpldm/entity.h"
#include "libpldm/state_set_oem_ibm.h"

#include "host-bmc/dbus/custom_dbus.hpp"

namespace pldm
{
namespace dbus
{

bool PCIETopology::pcIeTopologyRefresh() const
{
    return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
        pcIeTopologyRefresh();
}

bool PCIETopology::updateTopologyRefresh()
{
    stateOfCallback.first = false;
    stateOfCallback.second = false;
    return sdbusplus::com::ibm::PLDM::server::PCIeTopology::pcIeTopologyRefresh(
        true);
}

bool PCIETopology::callbackGetPCIeTopology(bool value)
{
    stateOfCallback.first = value;

    if (stateOfCallback.first && stateOfCallback.second)
    {
        // if both the states of the effecter are set only then update the
        // property
        return updateTopologyRefresh();
    }
    return false;
}

bool PCIETopology::callbackGetCableInfo(bool value)
{
    stateOfCallback.second = value;

    if (stateOfCallback.first && stateOfCallback.second)
    {
        // if both the states of the effecter are set only then update the
        // property
        return updateTopologyRefresh();
    }
    return false;
}

bool PCIETopology::pcIeTopologyRefresh(bool value)
{
    std::vector<set_effecter_state_field> stateField;
    std::vector<set_effecter_state_field> stateField1;

    if (value ==
        sdbusplus::com::ibm::PLDM::server::PCIeTopology::pcIeTopologyRefresh())
    {
        stateField.push_back({PLDM_NO_CHANGE, 0});
        return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
            pcIeTopologyRefresh(value);
    }
    else
    {
        stateField.push_back({PLDM_REQUEST_SET, GET_PCIE_TOPOLOGY});
        stateField1.push_back({PLDM_REQUEST_SET, GET_CABLE_INFO});
    }

    if (value && hostEffecterParser)
    {
        uint16_t effecterID = getEffecterID();

        if (effecterID == 0)
        {
            return false;
        }
        // callback is done only when setting the effecter is successful
        hostEffecterParser->sendSetStateEffecterStates(
            mctpEid, effecterID, 1, stateField,
            std::bind(
                std::mem_fn(&pldm::dbus::PCIETopology::callbackGetPCIeTopology),
                this, std::placeholders::_1),
            value);

        hostEffecterParser->sendSetStateEffecterStates(
            mctpEid, effecterID, 1, stateField1,
            std::bind(
                std::mem_fn(&pldm::dbus::PCIETopology::callbackGetCableInfo),
                this, std::placeholders::_1),
            value);
    }

    // Set the property to true only when the two set state effecter calls are
    // successful
    if (stateOfCallback.first && stateOfCallback.second)
    {
        return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
            pcIeTopologyRefresh(true);
    }
    else
    {
        return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
            pcIeTopologyRefresh(false);
    }
}

bool PCIETopology::savePCIeTopologyInfo() const
{
    return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
        savePCIeTopologyInfo();
}

bool PCIETopology::savePCIeTopologyInfo(bool value)
{
    std::vector<set_effecter_state_field> stateField;

    if (value ==
        sdbusplus::com::ibm::PLDM::server::PCIeTopology::savePCIeTopologyInfo())
    {
        stateField.push_back({PLDM_NO_CHANGE, 0});
        return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
            savePCIeTopologyInfo(value);
    }
    else
    {
        stateField.push_back({PLDM_REQUEST_SET, SAVE_PCIE_TOPLOGY});
    }

    if (value && hostEffecterParser)
    {
        uint16_t effecterID = getEffecterID();

        if (effecterID == 0)
        {
            return false;
        }

        hostEffecterParser->sendSetStateEffecterStates(
            mctpEid, effecterID, 1, stateField, nullptr, value);
        return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
            savePCIeTopologyInfo(false);
    }
    return sdbusplus::com::ibm::PLDM::server::PCIeTopology::
        savePCIeTopologyInfo(value);
}

uint16_t PCIETopology::getEffecterID()
{
    uint16_t effecterID = 0;

    pldm::pdr::EntityType entityType = PLDM_ENTITY_GROUP | 0x8000;
    auto stateEffecterPDRs = pldm::utils::findStateEffecterPDR(
        mctpEid, entityType,
        static_cast<uint16_t>(PLDM_OEM_IBM_PCIE_TOPOLOGY_ACTIONS),
        hostEffecterParser->getPldmPDR());
    if (stateEffecterPDRs.empty())
    {
        std::cerr
            << "PCIe Topology: The state set PDR can not be found, entityType = "
            << entityType << std::endl;
        return effecterID;
    }
    for (auto& rep : stateEffecterPDRs)
    {
        auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(rep.data());
        effecterID = pdr->effecter_id;
        break;
    }
    return effecterID;
}

} // namespace dbus
} // namespace pldm
