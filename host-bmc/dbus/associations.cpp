#include "associations.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{
AssociationsObj Associations::associations() const
{
    return sdbusplus::xyz::openbmc_project::Association::server::Definitions::
        associations();
}

AssociationsObj Associations::associations(AssociationsObj value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "Associations",
                                                         "associations", value);

    return sdbusplus::xyz::openbmc_project::Association::server::Definitions::
        associations(value);
}

} // namespace dbus
} // namespace pldm
