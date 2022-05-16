#include "linkreset.hpp"

namespace pldm
{
namespace dbus
{

bool link::linkReset(bool value)
{
    return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset(
        value);
}

bool link::linkReset() const
{
    return sdbusplus::com::ibm::Control::Host::server::PCIeLink::linkReset();
}

} // namespace dbus
} // namespace pldm
