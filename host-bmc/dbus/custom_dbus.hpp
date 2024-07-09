#pragma once

#include "com/ibm/License/Entry/LicenseEntry/server.hpp"
#include "../dbus_to_terminus_effecters.hpp"
#include "asset.hpp"
#include "associations.hpp"
#include "availability.hpp"
#include "board.hpp"
#include "cable.hpp"
#include "chassis.hpp"
#include "chapdata.hpp"
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
#include "pcie_device.hpp"
#include "pcie_slot.hpp"
#include "power_supply.hpp"
#include "vrm.hpp"
#include "com/ibm/License/Entry/LicenseEntry/server.hpp"

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>
#include <xyz/openbmc_project/State/Decorator/Availability/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>
#include <libpldm/state_set.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>

#include <map>
#include <memory>
#include <string>

#include <libpldm/state_set.h>

namespace pldm
{
namespace dbus
{

using LocationIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::Inventory::
                                    Decorator::server::LocationCode>;
using LicIntf = sdbusplus::server::object_t<
    sdbusplus::com::ibm::License::Entry::server::LicenseEntry>;
using AvailabilityIntf = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::State::Decorator::server::Availability>;
using OperationalStatusIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::State::
                                    Decorator::server::OperationalStatus>;
using ObjectPath = std::string;

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
     *  @param[in] path - The object path
     *
     *  @return std::optional<std::string> - The value of the LocationCode
     *          property
     */
    std::optional<std::string> getLocationCode(const std::string& path) const;
    /** @brief Implement CpuCore Interface
     *
     *  @param[in] path - The object path
     *
     */
    void implementCpuCoreInterface(const std::string& path);
    /** @brief Set the microcode property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] value  - microcode value
     */
    void setMicroCode(const std::string& path, uint32_t value);

    /** @brief Get the microcode property
     *
     *  @param[in] path   - The object path
     *
     *  @return std::optional<uint32_t> - The value of the microcode value
     */
    std::optional<uint32_t> getMicroCode(const std::string& path) const;

    /** @brief Implement PCIeSlot Interface
     *
     *  @param[in] path - the object path
     */
    void implementPCIeSlotInterface(const std::string& path);

    /** @brief Set the slot type
     *
     *  @param[in] path - the object path
     *  @param[in] slot type - Slot type
     */
    void setSlotType(const std::string& path, const std::string& slotType);

    /** @brief Implement PCIe Device Interface
     *
     *  @param[in] path - the object path
     */
    void implementPCIeDeviceInterface(const std::string& path);

    /** @brief Set PCIe Device Lanes in use property
     *
     *  @param[in] path - the object path
     *  @param[in] lanesInUse - Lanes in use
     *  @param[in] value - Generation in use
     */
    void setPCIeDeviceProps(const std::string& path, size_t lanesInUse,
                            const std::string& value);

    /** @brief Implement PCIe Cable Interface
     *
     *  @param[in] path - the object path
     */
    void implementCableInterface(const std::string& path);

    /** @brief set cable attributes
     *
     *  @param[in] path - the object path
     *  @param[in] length - length of the wire
     *  @param[in] cableDescription - cable details
     */
    void setCableAttributes(const std::string& path, double length,
                            const std::string& cableDescription);
    /** @brief Implement interface for motherboard property
     *
     *  @param[in] path  - The object path
     *
     */
    void implementMotherboardInterface(const std::string& path);

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

    /** @brief Implement ChapData  interface
     *
     *  @param[in] path - The object path
     *  @param[in] dbusToFilehandlerObj - virtual pointer to raise
     * NewChapdataFileAvailable method in oem layer request
     */
    void implementChapDataInterface(
        const std::string& path,
        pldm::responder::oem_fileio::Handler* dbusToFilehandlerObj);

    /** @brief Set the Inventory Item property
     *  @param[in] path - The object path
     *  @param[in] bool - the presence of fru
     */
    void updateItemPresentStatus(const std::string& path, bool isPresent);

    /** @brief get Bus ID
     *  @param[in] path - The object path
     */
    size_t getBusId(const std::string& path) const;

    /** @brief Implement Chassis Interface
     *  @param[in] path - the object path
     */
    void implementChassisInterface(const std::string& path);

    void implementPowerSupplyInterface(const std::string& path);

    void implementFanInterface(const std::string& path);

    void implementConnecterInterface(const std::string& path);

    void implementVRMInterface(const std::string& path);

    void implementFabricAdapter(const std::string& path);

    void implementBoard(const std::string& path);

    void implementGlobalInterface(const std::string& path);

    void implementAssetInterface(const std::string& path);

    void implementPanelInterface(const std::string& path);

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
     *
     *  @param[in] authdevno - License name
     *
     *  @param[in] name      - License name
     *
     *  @param[in] serialno  - License serial number
     *
     *  @param[in] exptime   - License expiration time
     *
     *  @param[in] type      - License type
     *
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

    /** @brief Implement Enable interface
     *
     *  @param[in] path  - The object path
     *
     */
    void implementObjectEnableIface(const std::string& path, bool value);

    /** @brief Remove DBus objects from cache
     *
     *  @param[in] types  - entity type
     */
    void removeDBus(const std::vector<uint16_t> types);

    /** @brief Remove all DBus object paths from cache
     *
     *  @param[in] types  - entity type
     */
    void deleteObject(const std::string& path);

    /** @brief set properties  on slots */
    void setSlotProperties(const std::string& path, const uint32_t& value,
                           const std::string& linkState);

    /** @brief set pcie device properties */
    void setPCIeDeviceProps(const std::string& path, int64_t lanesInuse,
                            const std::string& value);

    /** @brief set cable attributes */
    void setCableAttributes(const std::string& path, double length,
                            const std::string& cableDescription,
                            const std::string& cableStatus);

    /* @brief set partNumber */
    void setPartNumber(const std::string& path, const std::string& partNumber);

  private:
    std::unordered_map<ObjectPath, std::unique_ptr<LocationIntf>> location;
    std::unordered_map<ObjectPath, std::unique_ptr<CPUCore>> cpuCore;
    std::unordered_map<ObjectPath, std::unique_ptr<PCIeDevice>> pcieDevice;
    std::unordered_map<ObjectPath, std::unique_ptr<PCIeSlot>> pcieSlot;
    std::unordered_map<ObjectPath, std::unique_ptr<Cable>> cable;
    std::unordered_map<ObjectPath, std::unique_ptr<Motherboard>> motherboard;
    std::map<ObjectPath, std::unique_ptr<OperationalStatusIntf>> operationalStatus;
    std::map<ObjectPath, std::unique_ptr<AvailabilityIntf>> availabilityState;
    std::unordered_map<ObjectPath, std::unique_ptr<LicIntf>> codLic;
    std::unordered_map<ObjectPath, std::unique_ptr<Enable>> enabledStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<InventoryItem>>
        presentStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<ItemChassis>> chassis;
    std::unordered_map<ObjectPath, std::unique_ptr<Fan>> fan;
    std::unordered_map<ObjectPath, std::unique_ptr<Connector>> connector;
    std::unordered_map<ObjectPath, std::unique_ptr<VRM>> vrm;
    std::unordered_map<ObjectPath, std::unique_ptr<Global>> global;
    std::unordered_map<ObjectPath, std::unique_ptr<PowerSupply>> powersupply;
    std::unordered_map<ObjectPath, std::unique_ptr<Board>> board;
    std::unordered_map<ObjectPath, std::unique_ptr<FabricAdapter>>
        fabricAdapter;
    std::unordered_map<ObjectPath, std::unique_ptr<ChapDatas>> chapdata;
    std::unordered_map<ObjectPath, std::unique_ptr<Associations>> associations;
    std::unordered_map<ObjectPath, std::unique_ptr<LEDGroup>> ledGroup;
    std::unordered_map<ObjectPath, std::unique_ptr<Asset>> asset;
};

} // namespace dbus
} // namespace pldm
