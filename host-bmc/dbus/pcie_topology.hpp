#pragma once

#include "libpldm/pdr.h"

#include "common/utils.hpp"
#include "platform-mc/dbus_to_terminus_effecters.hpp"

#include <com/ibm/PLDM/PCIeTopology/server.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <string>
#include <utility>

namespace pldm
{
namespace dbus
{
using TopologyObj = sdbusplus::server::object_t<
    sdbusplus::com::ibm::PLDM::server::PCIeTopology>;

class PCIETopology : public TopologyObj
{
  public:
    PCIETopology() = delete;
    ~PCIETopology() = default;
    PCIETopology(const PCIETopology&) = delete;
    PCIETopology& operator=(const PCIETopology&) = delete;
    PCIETopology(PCIETopology&&) = delete;
    PCIETopology& operator=(PCIETopology&&) = delete;

    PCIETopology(sdbusplus::bus_t& bus, const std::string& objPath,
                 pldm::host_effecters::HostEffecterParser* hostEffecterParser,
                 uint8_t mctpEid) :
        TopologyObj(bus, objPath.c_str()),
        hostEffecterParser(hostEffecterParser), mctpEid(mctpEid)

    {}

    bool pcIeTopologyRefresh(bool value) override;

    bool pcIeTopologyRefresh() const override;

    bool savePCIeTopologyInfo(bool value) override;

    bool savePCIeTopologyInfo() const override;

    uint16_t getEffecterID();

    bool updateTopologyRefresh();

    /** @brief callback to getPCIeTopology **/
    bool callbackGetPCIeTopology(bool value);

    /** @brief callback to getCableInfo **/
    bool callbackGetCableInfo(bool value);

  private:
    /** @brief Pointer to host effecter parser */
    pldm::host_effecters::HostEffecterParser* hostEffecterParser;

    /** mctp endpoint id */
    uint8_t mctpEid;

    /** Pair to indicate the state of callbacks */
    std::pair<bool, bool> stateOfCallback = std::make_pair(false, false);
};

} // namespace dbus
} // namespace pldm
