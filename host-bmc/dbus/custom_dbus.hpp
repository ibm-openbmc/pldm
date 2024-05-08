#pragma once

#include "common/utils.hpp"
#include "associations.hpp"
#include "cpu_core.hpp"
#include "motherboard.hpp"
#include "pcie_device.hpp"
#include "pcie_slot.hpp"
#include "../dbus_to_host_effecters.hpp"

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace pldm
{
namespace dbus
{
using ObjectPath = std::string;

using LocationIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Decorator::server::LocationCode>;

/** @class CustomDBus
 *  @brief This is a custom D-Bus object, used to add D-Bus interface and update
 *         the corresponding properties value.
 */
class CustomDBus
{
  private:
    CustomDBus() {}

  public:
    CustomDBus(const CustomDBus&) = delete;
    CustomDBus(CustomDBus&&) = delete;
    CustomDBus& operator=(const CustomDBus&) = delete;
    CustomDBus& operator=(CustomDBus&&) = delete;
    ~CustomDBus() = default;

    static CustomDBus& getCustomDBus()
    {
        static CustomDBus customDBus;
        return customDBus;
    }

  public:
    /** @brief Set the LocationCode property
     *
     *  @param[in] path  - The object path
     *
     *  @param[in] value - The value of the LocationCode property
     */
    void setLocationCode(const std::string& path, std::string value);

    /** @brief Get the LocationCode property
     *
     *  @param[in] path  - The object path
     *
     *  @return std::optional<std::string> - The value of the LocationCode
     *          property
     */
    std::optional<std::string> getLocationCode(const std::string& path) const;

    /** @brief Implement CpuCore Interface
     *
     *  @param[in] path - The object path
     *
     *  @note This API will also implement the following interface
     *        xyz.openbmc_project.Object.Enable::Enabled dbus property
     *        which is mapped with the "Processor:Enabled" Redfish property
     *        to do either enable or disable the particular resource
     *        via Redfish client so the Enabled dbus property needs to host
     *        in the PLDM created core inventory item object.
     */
    void implementCpuCoreInterface(const std::string& path);

    /** @brief Set the microcode property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] value  - microcode value
     */
    void setMicrocode(const std::string& path, uint32_t value);

    /** @brief Implement interface for motherboard property
     *
     *  @param[in] path  - The object path
     *
     */
    void implementMotherboardInterface(const std::string& path);

    /** @brief Set the Associations property
     *
     *  @param[in] path     - The object path
     *
     *  @param[in] value    - An array of forward, reverse, endpoint tuples
     */
    void setAssociations(const std::string& path, AssociationsObj assoc);

    /** @brief Get the current Associations property
     *
     *  @param[in] path     - The object path
     *
     *  @return current Associations -  An array of forward, reverse, endpoint
     * tuples
     */
    const std::vector<std::tuple<std::string, std::string, std::string>>
        getAssociations(const std::string& path);

    /** @brief get Bus ID
     *  @param[in] path - The object path
     */
    size_t getBusId(const std::string& path) const;
    void implementPCIeSlotInterface(const std::string& path);
    void implementPCIeDeviceInterface(const std::string& path);
    /** @brief set properties  on slots */
    void setSlotProperties(const std::string& path, const uint32_t& value,
                           const std::string& linkState);

    /** @brief set pcie device properties */
    void setPCIeDeviceProps(const std::string& path, int64_t lanesInuse,
                            const std::string& value);

    /* @brief set partNumber */
    void setPartNumber(const std::string& path, const std::string& partNumber);

    void setSlotType(const std::string& path, const std::string& slotType);

  private:
    std::unordered_map<ObjectPath, std::unique_ptr<LocationIntf>> location;
    std::unordered_map<ObjectPath, std::unique_ptr<CPUCore>> cpuCore;
    std::unordered_map<ObjectPath, std::unique_ptr<Motherboard>> motherboard;
    std::unordered_map<ObjectPath, std::unique_ptr<Associations>> associations;
    std::unordered_map<ObjectPath, std::unique_ptr<PCIeDevice>> pcieDevice;
    std::unordered_map<ObjectPath, std::unique_ptr<PCIeSlot>> pcieSlot;
};

} // namespace dbus
} // namespace pldm
