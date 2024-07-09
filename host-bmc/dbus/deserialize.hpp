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

/** @brief Restoring Dbus values from persistent file.
 *
 *  @param[in] hostPDRHandler  - Host pdr handler class object pointer
 */
void restoreDbusObj(HostPDRHandler* hostPDRHandler);

} // namespace deserialize
} // namespace pldm
