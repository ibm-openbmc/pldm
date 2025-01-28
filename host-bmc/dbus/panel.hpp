#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Panel/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemPanel = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Panel>;

class Panel : public ItemPanel
{
  public:
    Panel() = delete;
    ~Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    Panel(Panel&&) = delete;
    Panel& operator=(Panel&&) = delete;

    Panel(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemPanel(bus, objPath.c_str()), path(objPath)
    {
        pldm::serialize::Serialize::getSerialize().serialize(path, "Panel");
    }

  private:
    std::string path;
};

} // namespace dbus
} // namespace pldm
