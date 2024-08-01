#include "enable.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{

bool Enable::enabled() const
{
    return sdbusplus::xyz::openbmc_project::Object::server::Enable::enabled();
}

bool Enable::enabled(bool value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "Enable",
                                                         "enabled", value);

    return sdbusplus::xyz::openbmc_project::Object::server::Enable::enabled(
        value);
}

} // namespace dbus
} // namespace pldm
