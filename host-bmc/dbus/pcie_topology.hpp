#pragma once

#include "libpldm/pdr.h"

#include "../dbus_to_host_effecters.hpp"
#include "common/utils.hpp"

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
using TopologyObj = sdbusplus::server::object::object<
    sdbusplus::com::ibm::PLDM::server::PCIeTopology>;

class PCIETopology : public TopologyObj
{
  public:
    PCIETopology() = delete;
    ~PCIETopology() = default;
    PCIETopology(const PCIETopology&) = delete;
    PCIETopology& operator=(const PCIETopology&) = delete;
    PCIETopology(PCIETopology&&) = default;
    PCIETopology& operator=(PCIETopology&&) = default;

    PCIETopology(sdbusplus::bus::bus& bus, const std::string& objPath,
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
