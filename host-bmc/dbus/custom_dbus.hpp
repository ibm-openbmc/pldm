#pragma once

#include "chapdata.hpp"
#include "com/ibm/License/Entry/LicenseEntry/server.hpp"
#include "common/utils.hpp"
#include "cpu_core.hpp"
#include "enable.hpp"
#include "motherboard.hpp"

#include <libpldm/state_set.h>

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/LocationCode/server.hpp>
#include <xyz/openbmc_project/State/Decorator/Availability/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

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
using LicIntf = sdbusplus::server::object_t<
    sdbusplus::com::ibm::License::Entry::server::LicenseEntry>;
using AvailabilityIntf = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::State::Decorator::server::Availability>;
using OperationalStatusIntf =
    sdbusplus::server::object_t<sdbusplus::xyz::openbmc_project::State::
                                    Decorator::server::OperationalStatus>;

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

    /** @brief Set the Functional property
     *
     *  @param[in] path   - The object path
     *
     *  @param[in] status - PLDM operational fault status
     */
    void setOperationalStatus(const std::string& path, uint8_t status);

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

    /** @brief Implement CpuCore Interface
     *
     *  @param[in] path - The object path
     *
     */
    void implementCpuCoreInterface(const std::string& path);

    /** @brief Set the setMicroCode property
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

    /** @brief Implement interface for motherboard property
     *
     *  @param[in] path  - The object path
     *
     */
    void implementMotherboardInterface(const std::string& path);

    /** @brief Implement Enable interface
     *
     *  @param[in] path  - The object path
     *
     */
    void implementObjectEnableIface(const std::string& path, bool value);

  private:
    std::unordered_map<ObjectPath, std::unique_ptr<LocationIntf>> location;
    std::map<ObjectPath, std::unique_ptr<OperationalStatusIntf>>
        operationalStatus;
    std::map<ObjectPath, std::unique_ptr<AvailabilityIntf>> availabilityState;
    std::unordered_map<ObjectPath, std::unique_ptr<LicIntf>> codLic;
    std::unordered_map<ObjectPath, std::unique_ptr<CPUCore>> cpuCore;
    std::unordered_map<ObjectPath, std::unique_ptr<Enable>> enabledStatus;
    std::unordered_map<ObjectPath, std::unique_ptr<Motherboard>> motherboard;
    std::unordered_map<ObjectPath, std::unique_ptr<ChapDatas>> chapdata;
};

} // namespace dbus
} // namespace pldm
