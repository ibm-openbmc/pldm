#pragma once

#include "host-bmc/host_pdr_handler.hpp"
#include "license_entry.hpp"
#include "type.hpp"

#include <filesystem>
#include <fstream>

namespace pldm
{
namespace deserialize
{

void restoreDbusObj(HostPDRHandler* hostPDRHandler);

void reSerialize(std::vector<uint16_t> type);

} // namespace deserialize
} // namespace pldm
