#pragma once

#include "serialize.hpp"

#include <com/ibm/Control/Host/PCIeLink/server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <string>

namespace pldm
{
namespace dbus
{

using Itemlink = sdbusplus::server::object::object<
    sdbusplus::com::ibm::Control::Host::server::PCIeLink>;

class link : public Itemlink
{
  public:
    link() = delete;
    ~link() = default;
    link(const link&) = delete;
    link& operator=(const link&) = delete;
    link(link&&) = default;
    link& operator=(link&&) = default;

    link(sdbusplus::bus::bus& bus, const std::string& objPath) :
        Itemlink(bus, objPath.c_str()), path(objPath)
    {
        // no need to save this in pldm memory
    }

    /** set link reset */
    bool linkReset(bool value) override;

    /** Get link reset state */
    bool linkReset() const override;

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
