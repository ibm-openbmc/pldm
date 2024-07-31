#include "decorator_revision.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{

std::string DecoratorRevision::version() const
{
    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Revision::
        version();
}

std::string DecoratorRevision::version(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(
        path, "DecoratorRevision", "version", value);

    return sdbusplus::xyz::openbmc_project::Inventory::Decorator::server::Revision::version(
        value);
}

} // namespace dbus
} // namespace pldm
