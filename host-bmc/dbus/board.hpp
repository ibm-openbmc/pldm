#pragma once

#include "serialize.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Inventory/Item/Board/server.hpp>

#include <string>

namespace pldm
{
namespace dbus
{
using ItemBoard = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Inventory::Item::server::Board>;

class Board : public ItemBoard
{
  public:
    Board() = delete;
    ~Board() = default;
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    Board(Board&&) = delete;
    Board& operator=(Board&&) = delete;

    Board(sdbusplus::bus_t& bus, const std::string& objPath) :
        ItemBoard(bus, objPath.c_str())
    {
        pldm::serialize::Serialize::getSerialize().serialize(objPath, "Board");
    }
};

} // namespace dbus
} // namespace pldm
