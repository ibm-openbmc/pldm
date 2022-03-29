#pragma once

#include "libpldm/pdr.h"

#include "../dbus_to_host_effecters.hpp"
#include "common/utils.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Led/Group/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using AssertedIntf = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Led::server::Group>;

class LEDGroup : public AssertedIntf
{
  public:
    LEDGroup() = delete;
    ~LEDGroup() = default;
    LEDGroup(const LEDGroup&) = delete;
    LEDGroup& operator=(const LEDGroup&) = delete;
    LEDGroup(LEDGroup&&) = default;
    LEDGroup& operator=(LEDGroup&&) = default;

    LEDGroup(sdbusplus::bus::bus& bus, const std::string& objPath,
             pldm::host_effecters::HostEffecterParser* hostEffecterParser,
             const pldm_entity entity, uint8_t mctpEid) :
        AssertedIntf(bus, objPath.c_str(), true),
        hostEffecterParser(hostEffecterParser), entity(entity), mctpEid(mctpEid)
    {
        // Emit deferred signal.
        emit_object_added();
    }

    /** @brief Property SET Override function
     *
     *  @param[in]  value   -  True or False
     *  @return             -  Success or exception thrown
     */
    bool asserted(bool value) override;

    bool asserted() const override;

    inline void setStateEffecterStatesFlag(bool value)
    {
        isTriggerStateEffecterStates = value;
    }

  private:
    bool updateAsserted(bool value);

  private:
    /** @brief Pointer to host effecter parser */
    pldm::host_effecters::HostEffecterParser* hostEffecterParser;

    const pldm_entity entity;

    uint8_t mctpEid;

    bool isTriggerStateEffecterStates = false;
};

} // namespace dbus
} // namespace pldm
