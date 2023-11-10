#include "chapdata.hpp"

#include "serialize.hpp"

namespace pldm
{
namespace dbus
{

std::string ChapDatas::chapName(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "ChapData",
                                                         "chapName", value);

    return sdbusplus::com::ibm::PLDM::server::ChapData::chapName(value);
}

std::string ChapDatas::chapName() const
{
    return sdbusplus::com::ibm::PLDM::server::ChapData::chapName();
}

std::string ChapDatas::chapSecret(std::string value)
{
    pldm::serialize::Serialize::getSerialize().serialize(path, "ChapData",
                                                         "chapSecret", value);
    if (chapName() != "" && value != "")
    {
        dbusToFilehandler->newChapDataFileAvailable(chapName(), value);
    }
    return sdbusplus::com::ibm::PLDM::server::ChapData::chapSecret(value);
}

std::string ChapDatas::chapSecret() const
{
    return sdbusplus::com::ibm::PLDM::server::ChapData::chapSecret();
}

} // namespace dbus
} // namespace pldm
