#pragma once

#include "platform-mc/dbus_to_terminus_effecters.hpp"
#include "serialize.hpp"

#ifdef OEM_IBM
#include "oem/ibm/libpldmresponder/utils.hpp"
#endif

#include <com/ibm/Control/Host/PCIeLink/server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <string>
namespace pldm
{
namespace dbus
{
using Itemlink = sdbusplus::server::object_t<
    sdbusplus::com::ibm::Control::Host::server::PCIeLink>;

class Link : public Itemlink
{
  public:
    Link() = delete;
    ~Link() = default;
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;
    Link(Link&&) = delete;
    Link& operator=(Link&&) = delete;

    Link(sdbusplus::bus_t& bus, const std::string& objPath,
         pldm::host_effecters::HostEffecterParser* hostEffecterParser,
         uint8_t mctpEid) :
        Itemlink(bus, objPath.c_str()), path(objPath),
        hostEffecterParser(hostEffecterParser), mctpEid(mctpEid)
    {
        // no need to save this in pldm memory
    }
    /** set link reset */
    bool linkReset(bool value) override;
    /** Get link reset state */
    bool linkReset() const override;

    uint16_t getEffecterID();

  private:
    std::string path;

    /** @brief Pointer to host effecter parser */
    pldm::host_effecters::HostEffecterParser* hostEffecterParser;

    /** mctp endpoint id */
    uint8_t mctpEid;
};

} // namespace dbus
} // namespace pldm
