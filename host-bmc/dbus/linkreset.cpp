#include "linkreset.hpp"

#include "libpldm/entity.h"
#include "libpldm/state_set.h"

#include <libpldm/states.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace dbus
{

bool Link::linkReset(bool value)
{
    error("CustomDBus: Got a link reset on: {PATH}", "PATH", path);
    std::vector<set_effecter_state_field> stateField;

    if (value ==
        sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset())
    {
        stateField.push_back({PLDM_NO_CHANGE, 0});
        return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset(
            value);
    }
    else
    {
        stateField.push_back({PLDM_REQUEST_SET, PLDM_RESETTING});
    }

    if (value && hostEffecterParser)
    {
        uint16_t effecterID = getEffecterID();

        if (effecterID == 0)
        {
            return false;
        }

        error(
            "CustomDBus: Sending a effecter call to host with effecter id: {EFF_ID}",
            "EFF_ID", effecterID);
        hostEffecterParser->sendSetStateEffecterStates(
            mctpEid, effecterID, 1, stateField, nullptr, value);
        error(
            "Link reset on path : {PATH}  is successful setting it back to false",
            "PATH", path);
        return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset(
            false);
    }
    return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset(
        value);
}

uint16_t Link::getEffecterID()
{
    uint16_t effecterID = 0;

    pldm::pdr::EntityType entityType = PLDM_ENTITY_PCI_EXPRESS_BUS | 0x8000;
    auto stateEffecterPDRs = pldm::utils::findStateEffecterPDR(
        mctpEid, entityType, static_cast<uint16_t>(PLDM_STATE_SET_AVAILABILITY),
        hostEffecterParser->getPldmPDR());

    if (stateEffecterPDRs.empty())
    {
        error(
            "PCIe LinkReset: The state set PDR can not be found, entityType = {ENTITY_TYP}",
            "ENTITY_TYP", entityType);
        return effecterID;
    }

    uint32_t entityInstance = 0;
#ifdef OEM_IBM
    entityInstance = pldm::responder::utils::getLinkResetInstanceNumber(path);
#endif

    error("CustomDBus: BusID of the link is: {ENTITY_INST}", "ENTITY_INST",
          entityInstance);
    for (auto& rep : stateEffecterPDRs)
    {
        auto pdr = reinterpret_cast<pldm_state_effecter_pdr*>(rep.data());
        if (entityInstance == (uint32_t)pdr->entity_instance)
        {
            effecterID = pdr->effecter_id;
            break;
        }
    }
    return effecterID;
}

bool Link::linkReset() const
{
    return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset();
}
} // namespace dbus
} // namespace pldm
