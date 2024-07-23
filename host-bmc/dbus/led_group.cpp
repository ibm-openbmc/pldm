#include "led_group.hpp"

#include "libpldm/state_set.h"

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace dbus
{
bool LEDGroup::asserted() const
{
    info("inside Asserted");
    return sdbusplus::xyz::openbmc_project::Led::server::Group::asserted();
}

bool LEDGroup::updateAsserted(bool value)
{
    info("inside updateAsserted");
    return sdbusplus::xyz::openbmc_project::Led::server::Group::asserted(value);
}

bool LEDGroup::asserted(bool value)
{
    info("Asserted with value {VAL}", "VAL", (int)value);
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
    info("TriggetStateEffecter {EFF}", "EFF", (int)isTriggerStateEffecterStates);
    if (isTriggerStateEffecterStates)
    {
        if (hostEffecterParser)
        {
            uint16_t effecterId = pldm::utils::findStateEffecterId(
                hostEffecterParser->getPldmPDR(), entity.entity_type,
                entity.entity_instance_num, entity.entity_container_id,
                PLDM_STATE_SET_IDENTIFY_STATE, false);
            auto curVal = asserted() ? "true" : "false";
            error(
                "Setting the led on : [ {OBJ_PATH} ], [{ENTITY_TYP}, {ENTITY_NUM}, {ENTITY_ID}] effecter ID : [ {EFFECTER_ID} ] , current value : [ {CUR_VAL} ], new value : [ {NEW_VAL} ]",
                "OBJ_PATH", objectPath, "ENTITY_TYP",
                static_cast<unsigned>(entity.entity_type), "ENTITY_NUM",
                static_cast<unsigned>(entity.entity_instance_num), "ENTITY_ID",
                static_cast<unsigned>(entity.entity_container_id),
                "EFFECTER_ID", effecterId, "CUR_VAL", curVal, "NEW_VAL", value);
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
