#pragma once

#include "libpldm/pdr.h"

#include "../dbus_to_host_effecters.hpp"
#include "associations.hpp"
#include "availability.hpp"
#include "board.hpp"
#include "chassis.hpp"
#include "common/utils.hpp"
#include "connector.hpp"
#include "cpu_core.hpp"
#include "enable.hpp"
#include "fabric_adapter.hpp"
#include "fan.hpp"
#include "global.hpp"
#include "inventory_item.hpp"
#include "led_group.hpp"
#include "license_entry.hpp"
#include "location_code.hpp"
#include "motherboard.hpp"
#include "operational_status.hpp"
#include "pcie_slot.hpp"
#include "power_supply.hpp"
#include "software_version.hpp"
#include "vrm.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <map>
#include <memory>
#include <string>

namespace pldm
{
namespace dbus
{

using ObjectPath = std::string;

/** @class CustomDBus
 *  @brief This is a custom D-Bus object, used to add D-Bus interface and
 * update the corresponding properties value.
 */
class CustomDBus
{
  private:
    CustomDBus()
    {}

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
     *  @param[in] path     - The object path
     *
     *  @return std::string - The value of the LocationCode property
     */
    std::string getLocationCode(const std::string& path) const;

    /** @brief Set the Functional property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] status - PLDM operational fault status
     *  @param [in] parentChassis - The parent chassis of the FRU
     *
     */
    void setOperationalStatus(const std::string& path, bool status,
                              const std::string& parentChassis = "");

    /** @brief Get the Functional property
     *
     *  @param[in] path   - The object path
     *
     *  @return status    - PLDM operational fault status
     */
    bool getOperationalStatus(const std::string& path) const;

    /** @brief Set the Inventory Item property
     *  @param[in] path - The object path
     *  @param[in] bool - the presence of fru
     */
    void updateItemPresentStatus(const std::string& path, bool isPresent);

    /** @brief Implement CpuCore Interface
     *  @param[in] path - The object path
     *
     * @note This API will also implement the following interface
     *       xyz.openbmc_project.Object.Enable::Enabled dbus property
     *       which is mapped with the "Processor:Enabled" Redfish property
     *       to do either enable or disable the particular resource
     *       via Redfish client so the Enabled dbus property needs to host
     *       in the PLDM created core inventory item object.
     */
    void implementCpuCoreInterface(const std::string& path);

    /** @brief Implement Chassis Interface
     *  @param[in] path - the object path
     */
    void implementChassisInterface(const std::string& path);

    void implementPCIeSlotInterface(const std::string& path);

    void implementPowerSupplyInterface(const std::string& path);

    void implementFanInterface(const std::string& path);

    void implementConnecterInterface(const std::string& path);

    void implementVRMInterface(const std::string& path);

    void implementMotherboardInterface(const std::string& path);

    void implementFabricAdapter(const std::string& path);

    void implementBoard(const std::string& path);

    void implementGlobalInterface(const std::string& path);
    /**
     * @brief Implement the xyz.openbmc_project.Object.Enable interface
     *
     * @param[in] path - The object path to implement Enable interface
     */
    void implementObjectEnableIface(const std::string& path, bool value);
    /** @brief Set the Asserted property
     *
     *  @param[in] path     - The object path
     *  @param[in] entity   - pldm entity
     *  @param[in] value    - To assert a group, set to True. To de-assert a
     *                        group, set to False.
     *  @param[in] hostEffecterParser    - Pointer to host effecter parser
     *  @param[in] instanceId - instance Id
     *  @param[in] isTriggerStateEffecterStates - Trigger stateEffecterStates
     *                                            command flag, true: trigger
     */
    void setAsserted(
        const std::string& path, const pldm_entity& entity, bool value,
        pldm::host_effecters::HostEffecterParser* hostEffecterParser,
        uint8_t instanceId, bool isTriggerStateEffecterStates = false);

    /** @brief Get the Asserted property
     *
     *  @param[in] path   - The object path
     *
     *  @return asserted  - Asserted property
     */
    bool getAsserted(const std::string& path) const;

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

    /** @brief Implement the license interface properties
     *
     *  @param[in] path      - The object path
     *  @param[in] authdevno - License name
     *  @param[in] name      - License name
     *  @param[in] serialno  - License serial number
     *  @param[in] exptime   - License expiration time
     *  @param[in] type      - License type
     *  @param[in] authtype  - License authorization type
     *
     * @note This API implements the following interface
     *       com.ibm.License.Entry.LicenseEntry and associated
     *       dbus properties.
     */
    void implementLicInterfaces(
        const std::string& path, const uint32_t& authdevno,
        const std::string& name, const std::string& serialno,
        const uint64_t& exptime,
        const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::Type&
            type,
        const sdbusplus::com::ibm::License::Entry::server::LicenseEntry::
            AuthorizationType& authtype);

    /** @brief Set the availability state property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] state  - Availability state
     */
    void setAvailabilityState(const std::string& path, const bool& state);

    /** @brief Set the version property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] value  - version value
     */
    void setSoftwareVersion(const std::string& path, std::string value);

    /** @brief Set the microcode property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] value  - microcode value
     */
    void setMicrocode(const std::string& path, uint32_t value);

    /** @brief Remove DBus objects from cache
     *
     *  @param[in] types  - entity type
     */
    void removeDBus(const std::vector<uint16_t> types);

  private:
    std::unordered_map<ObjectPath, std::unique_ptr<LocationCode>> location;
    std::unordered_map<ObjectPath, std::unique_ptr<OperationalStatus>>
        operationalStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<InventoryItem>>
        presentStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<ItemChassis>> chassis;
    std::unordered_map<ObjectPath, std::unique_ptr<CPUCore>> cpuCore;
    std::unordered_map<ObjectPath, std::unique_ptr<Fan>> fan;
    std::unordered_map<ObjectPath, std::unique_ptr<Connector>> connector;
    std::unordered_map<ObjectPath, std::unique_ptr<VRM>> vrm;
    std::unordered_map<ObjectPath, std::unique_ptr<Global>> global;
    std::unordered_map<ObjectPath, std::unique_ptr<PowerSupply>> powersupply;
    std::unordered_map<ObjectPath, std::unique_ptr<Board>> board;
    std::unordered_map<ObjectPath, std::unique_ptr<FabricAdapter>>
        fabricAdapter;
    std::unordered_map<ObjectPath, std::unique_ptr<Motherboard>> motherboard;
    std::unordered_map<ObjectPath, std::unique_ptr<Availability>>
        availabilityState;
    std::unordered_map<ObjectPath, std::unique_ptr<Enable>> _enabledStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<PCIeSlot>> pcieSlot;
    std::unordered_map<ObjectPath, std::unique_ptr<LicenseEntry>> codLic;
    std::unordered_map<ObjectPath, std::unique_ptr<Associations>> associations;
    std::unordered_map<ObjectPath, std::unique_ptr<LEDGroup>> ledGroup;
    std::unordered_map<ObjectPath, std::unique_ptr<SoftWareVersion>>
        softWareVersion;

    /** @brief Remove all DBus object paths from cache
     *
     *  @param[in] types  - entity type
     */
    void deleteObject(const std::string& path);
};

} // namespace dbus
} // namespace pldm
