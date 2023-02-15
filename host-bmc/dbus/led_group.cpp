#include "led_group.hpp"

#include "libpldm/state_set.h"

namespace pldm
{
namespace dbus
{
bool LEDGroup::asserted() const
{
    return sdbusplus::xyz::openbmc_project::Led::server::Group::asserted();
}

bool LEDGroup::updateAsserted(bool value)
{
    return sdbusplus::xyz::openbmc_project::Led::server::Group::asserted(value);
}

bool LEDGroup::asserted(bool value)
{
    std::vector<set_effecter_state_field> stateField;

    if (value ==
        sdbusplus::xyz::openbmc_project::Led::server::Group::asserted())
    {
        stateField.push_back({PLDM_NO_CHANGE, 0});
    }
    else
    {
        uint8_t state = value ? PLDM_STATE_SET_IDENTIFY_STATE_ASSERTED
                              : PLDM_STATE_SET_IDENTIFY_STATE_UNASSERTED;

        stateField.push_back({PLDM_REQUEST_SET, state});
    }

    if (isTriggerStateEffecterStates)
    {
        if (hostEffecterParser)
        {
            uint16_t effecterId = pldm::utils::findStateEffecterId(
                hostEffecterParser->getPldmPDR(), entity.entity_type,
                entity.entity_instance_num, entity.entity_container_id,
                PLDM_STATE_SET_IDENTIFY_STATE, false);
            std::cerr << "Setting the led on : [ " << objectPath << "] ,[ "
                      << entity.entity_type << " , "
                      << entity.entity_instance_num << " , "
                      << entity.entity_container_id << " ] , effecter ID : [ "
                      << effecterId << " ] , current value : [ "
                      << std::boolalpha << asserted() << " ] new value : [ "
                      << value << " ] " << std::endl;
            hostEffecterParser->sendSetStateEffecterStates(
                mctpEid, effecterId, 1, stateField,
                std::bind(std::mem_fn(&pldm::dbus::LEDGroup::updateAsserted),
                          this, std::placeholders::_1),
                value);
            isTriggerStateEffecterStates = true;
            return value;
        }
    }

    isTriggerStateEffecterStates = true;
    return sdbusplus::xyz::openbmc_project::Led::server::Group::asserted(value);
}

} // namespace dbus
} // namespace pldm
