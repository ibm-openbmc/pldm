#pragma once

#include "../../libpldmresponder/oem_handler.hpp"

#include <com/ibm/PLDM/ChapData/server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ChapDataObj =
    sdbusplus::server::object_t<sdbusplus::com::ibm::PLDM::server::ChapData>;

/** @brief Hosting chapdata properties
 *
 *  @param[in] bus - dbus object to host properties into dbus path
 *  @param[in] objPath - dbus path to host two properties
 *  @param[in] dbusToFilehandlerObj - To handle file io operation
 */
class ChapDatas : public ChapDataObj
{
  public:
    ChapDatas() = delete;
    ~ChapDatas() = default;
    ChapDatas(const ChapDatas&) = delete;
    ChapDatas& operator=(const ChapDatas&) = delete;
    ChapDatas(ChapDatas&&) = default;
    ChapDatas& operator=(ChapDatas&&) = default;

    ChapDatas(sdbusplus::bus_t& bus, const std::string& objPath,
              pldm::responder::oem_fileio::Handler* dbusToFilehandlerObj) :
        ChapDataObj(bus, objPath.c_str()),
        dbusToFilehandler(dbusToFilehandlerObj), path(objPath)
    {}

    std::string chapName(std::string value) override;

    std::string chapName() const override;

    std::string chapSecret(std::string value) override;

    std::string chapSecret() const override;

  private:
    /** @brief Pointer to host effecter parser */
    pldm::responder::oem_fileio::Handler* dbusToFilehandler;
    std::string path;
};

} // namespace dbus
} // namespace pldm
