#include "operational_status.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
bool OperationalStatus::functional() const
{
    return sdbusplus::xyz::openbmc_project::State::Decorator::server::
        OperationalStatus::functional();
}

bool OperationalStatus::functional(bool value)
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "OperationalStatus", "functional", value);

    return sdbusplus::xyz::openbmc_project::State::Decorator::server::
        OperationalStatus::functional(value);
}

} // namespace dbus
} // namespace pldm
