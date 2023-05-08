#pragma once
#include "libpldm/entity.h"
#include "libpldm/pdr_oem_ibm.h"
#include "libpldm/platform.h"
#include "libpldm/state_set_oem_ibm.h"

#include "collect_slot_vpd.hpp"
#include "common/types.hpp"
#include "common/utils.hpp"
#include "inband_code_update.hpp"
#include "libpldmresponder/oem_handler.hpp"
#include "libpldmresponder/pdr_utils.hpp"
#include "libpldmresponder/platform.hpp"
#include "requester/handler.hpp"
#include "utils.hpp"

typedef ibm_oem_pldm_state_set_firmware_update_state_values CodeUpdateState;

namespace pldm
{
namespace responder
{
using ObjectPath = std::string;
using AssociatedEntityMap = std::map<ObjectPath, pldm_entity>;
namespace oem_ibm_platform
{
using AttributeName = std::string;
using AttributeType = std::string;
using ReadonlyStatus = bool;
using DisplayName = std::string;
using Description = std::string;
using MenuPath = std::string;
using CurrentValue = std::variant<int64_t, std::string>;
using DefaultValue = std::variant<int64_t, std::string>;
using OptionString = std::string;
using OptionValue = std::variant<int64_t, std::string>;
using Option = std::vector<std::tuple<OptionString, OptionValue>>;
using BIOSTableObj =
    std::tuple<AttributeType, ReadonlyStatus, DisplayName, Description,
               MenuPath, CurrentValue, DefaultValue, Option>;
using BaseBIOSTable = std::map<AttributeName, BIOSTableObj>;

using PendingObj = std::tuple<AttributeType, CurrentValue>;
using PendingAttributes = std::map<AttributeName, PendingObj>;
static constexpr auto PLDM_OEM_IBM_ENTITY_FIRMWARE_UPDATE = 24577;
static constexpr auto PLDM_OEM_IBM_FRONT_PANEL_TRIGGER = 32837;
static constexpr auto PLDM_OEM_IBM_ENTITY_REAL_SAI = 24581;
static constexpr auto PLDM_OEM_IBM_CHASSIS_POWER_CONTROLLER = 24580;

constexpr uint16_t ENTITY_INSTANCE_0 = 0;
constexpr uint16_t ENTITY_INSTANCE_1 = 1;

const pldm::pdr::TerminusID HYPERVISOR_TID = 208;

static constexpr uint8_t HEARTBEAT_TIMEOUT_DELTA = 10;
struct InstanceInfo
{
    uint8_t procId;
    uint8_t dcmId;
};
using HostEffecterInstanceMap = std::map<pldm::pdr::EffecterID, InstanceInfo>;

enum SetEventReceiverCount
{
    SET_EVENT_RECEIVER_SENT = 0x2,
};

class Handler : public oem_platform::Handler
{
  public:
    Handler(const pldm::utils::DBusHandler* dBusIntf,
            pldm::responder::CodeUpdate* codeUpdate,
            pldm::responder::SlotHandler* slotHandler, int mctp_fd,
            uint8_t mctp_eid, pldm::InstanceIdDb& instanceIdDb,
            sdeventplus::Event& event, pldm_pdr* repo,
            pldm::requester::Handler<pldm::requester::Request>* handler,
            pldm_entity_association_tree* bmcEntityTree,
            pldm::host_effecters::HostEffecterParser* hostEffecterParser) :
        oem_platform::Handler(dBusIntf),
        codeUpdate(codeUpdate), slotHandler(slotHandler),
        platformHandler(nullptr), mctp_fd(mctp_fd), mctp_eid(mctp_eid),
        instanceIdDb(instanceIdDb), event(event), pdrRepo(repo), handler(handler),
        bmcEntityTree(bmcEntityTree), hostEffecterParser(hostEffecterParser),
        timer(event, std::bind(std::mem_fn(&Handler::setSurvTimer), this,
                               HYPERVISOR_TID, false))

    {
        codeUpdate->setVersions();
        pldm::responder::utils::clearLicenseStatus();
        setEventReceiverCnt = 0;
        pldm::responder::utils::hostPCIETopologyIntf(mctp_eid,
                                                     hostEffecterParser);

        createMatches();

        using namespace sdbusplus::bus::match::rules;
        hostOffMatch = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            propertiesChanged("/xyz/openbmc_project/state/host0",
                              "xyz.openbmc_project.State.Host"),
            [this](sdbusplus::message::message& msg) {
            pldm::utils::DbusChangedProps props{};
            std::string intf;
            msg.read(intf, props);
            const auto itr = props.find("CurrentHostState");
            if (itr != props.end())
            {
                pldm::utils::PropertyValue value = itr->second;
                auto propVal = std::get<std::string>(value);
                if (propVal == "xyz.openbmc_project.State.Host.HostState.Off")
                {
                    hostOff = true;
                    setEventReceiverCnt = 0;
                    disableWatchDogTimer();
                    pldm::responder::utils::clearLicenseStatus();
                    pldm::responder::utils::clearDumpSocketWriteStatus();
                }
                else if (propVal ==
                         "xyz.openbmc_project.State.Host.HostState.Running")
                {
                    hostOff = false;
                    hostTransitioningToOff = false;
                }
                else if (
                    propVal ==
                    "xyz.openbmc_project.State.Host.HostState.TransitioningToOff")
                {
                    hostTransitioningToOff = true;
                }
            }
            });
        updateBIOSMatch = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            propertiesChanged("/xyz/openbmc_project/bios_config/manager",
                              "xyz.openbmc_project.BIOSConfig.Manager"),
            [this, codeUpdate](sdbusplus::message::message& msg) {
            constexpr auto propertyName = "PendingAttributes";
            using Value =
                std::variant<std::string, PendingAttributes, BaseBIOSTable>;
            using Properties = std::map<pldm::utils::DbusProp, Value>;
            Properties props{};
            std::string intf;
            msg.read(intf, props);

            auto valPropMap = props.find(propertyName);
            if (valPropMap == props.end())
            {
                return;
            }

            PendingAttributes pendingAttributes =
                std::get<PendingAttributes>(valPropMap->second);
            for (auto it : pendingAttributes)
            {
                if (it.first == "fw_boot_side")
                {
                    auto& [attributeType, attributevalue] = it.second;
                    std::string nextBootSideAttr =
                        std::get<std::string>(attributevalue);
                    std::string nextBootSide =
                        (nextBootSideAttr == "Perm" ? Pside : Tside);
                    codeUpdate->setNextBootSide(nextBootSide);
                }
            }
            });
        ibmCompatibleMatch = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::sender(
                    "xyz.openbmc_project.EntityManager"),
            std::bind(&Handler::ibmCompatibleAddedCallback, this,
                      std::placeholders::_1));

        stateManagerMatch = std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            sdbusplus::bus::match::rules::interfacesAdded() +
                sdbusplus::bus::match::rules::argNpath(
                    0, "/xyz/openbmc_project/state/host0"),
            [this](sdbusplus::message::message& msg) {
            sdbusplus::message::object_path path;
            std::map<std::string, std::map<std::string, dbus::Value>>
                interfaces;
            msg.read(path, interfaces);

            if (!interfaces.contains("xyz.openbmc_project.State.Host"))
            {
                return;
            }

            const auto& properties =
                interfaces.at("xyz.openbmc_project.State.Host");

            if (!properties.contains("RestartCause"))
            {
                return;
            }

            restartCause = std::get<std::string>(properties.at("RestartCause"));
            setBootTypesBiosAttr(restartCause);
            });
    }

    void ibmCompatibleAddedCallback(sdbusplus::message::message& msg)
    {
        sdbusplus::message::object_path path;
        std::map<std::string,
                 std::map<std::string, std::variant<std::vector<std::string>>>>
            interfaces;

        msg.read(path, interfaces);

        if (!interfaces.contains(
                "xyz.openbmc_project.Configuration.IBMCompatibleSystem"))
        {
            return;
        }

        // Get the "Name" property value of the
        // "xyz.openbmc_project.Configuration.IBMCompatibleSystem" interface
        const auto& properties = interfaces.at(
            "xyz.openbmc_project.Configuration.IBMCompatibleSystem");

        if (!properties.contains("Names"))
        {
            return;
        }
        auto names = std::get<std::vector<std::string>>(properties.at("Names"));

        // get only the first system type
        systemType = names[0];

        ibmCompatibleMatch.reset();
    }

    int oemSetNumericEffecterValueHandler(
        uint16_t entityType, uint16_t entityInstance,
        uint16_t effecterSemanticId, uint8_t effecterDataSize,
        uint8_t* effecterValue, real32_t effecterOffset,
        real32_t effecterResolution, uint16_t effecterId);

    int getOemStateSensorReadingsHandler(
        pldm::pdr::EntityType entityType,
        pldm::pdr::EntityInstance entityInstance,
        pldm::pdr::ContainerID containerId, pldm::pdr::StateSetId stateSetId,
        pldm::pdr::CompositeCount compSensorCnt, uint16_t sensorId,
        std::vector<get_sensor_state_field>& stateField);

    int oemSetStateEffecterStatesHandler(
        uint16_t entityType, uint16_t entityInstance, uint16_t stateSetId,
        uint8_t compEffecterCnt,
        std::vector<set_effecter_state_field>& stateField, uint16_t effecterId);

    /** @brief Method to set the platform handler in the
     *         oem_ibm_handler class
     *  @param[in] handler - pointer to PLDM platform handler
     */
    void setPlatformHandler(pldm::responder::platform::Handler* handler);

    /** @brief Method to fetch the effecter ID of the code update PDRs
     *
     * @return platformHandler->getNextEffecterId() - returns the
     *             effecter ID from the platform handler
     */
    virtual uint16_t getNextEffecterId()
    {
        return platformHandler->getNextEffecterId();
    }

    /** @brief Method to fetch the sensor ID of the code update PDRs
     *
     * @return platformHandler->getNextSensorId() - returns the
     *             Sensor ID from the platform handler
     */
    virtual uint16_t getNextSensorId()
    {
        return platformHandler->getNextSensorId();
    }

    virtual const AssociatedEntityMap& getAssociateEntityMap()
    {
        return platformHandler->getAssociateEntityMap();
    }

    /** @brief Method to Generate the OEM PDRs
     *
     * @param[in] repo - instance of concrete implementation of Repo
     */
    void buildOEMPDR(pdr_utils::Repo& repo);

    /** @brief Method to send code update event to host
     * @param[in] sensorId - sendor ID
     * @param[in] sensorEventClass - event class of sensor
     * @param[in] sensorOffset - sensor offset
     * @param[in] eventState - new code update event state
     * @param[in] prevEventState - previous code update event state
     * @return none
     */
    void sendStateSensorEvent(uint16_t sensorId,
                              enum sensor_event_class_states sensorEventClass,
                              uint8_t sensorOffset, uint8_t eventState,
                              uint8_t prevEventState);

    /** @brief Method to send encoded request msg of code update event to host
     *  @param[in] requestMsg - encoded request msg
     *  @param[in] instanceId - instance id of the message
     *  @return PLDM status code
     */
    int sendEventToHost(std::vector<uint8_t>& requestMsg, uint8_t instanceId);

    /** @brief _processEndUpdate processes the actual work that needs
     *  to be carried out after EndUpdate effecter is set. This is done async
     *  after sending response for EndUpdate set effecter
     *  @param[in] source - sdeventplus event source
     */
    void _processEndUpdate(sdeventplus::source::EventBase& source);

    /** @brief _processStartUpdate processes the actual work that needs
     *  to be carried out after StartUpdate effecter is set. This is done async
     *  after sending response for StartUpdate set effecter
     *  @param[in] source - sdeventplus event source
     */
    void _processStartUpdate(sdeventplus::source::EventBase& source);

    /** @brief _processSystemReboot processes the actual work that needs to be
     *  carried out after the System Power State effecter is set to reboot
     *  the system
     *  @param[in] source - sdeventplus event source
     */
    void _processSystemReboot(sdeventplus::source::EventBase& source);

    /** @brief setNumericEffecter
     *
     *  @param[in] entityInstance - the entity Instance
     *  @param[in] propertyValue - the value to be set
     *
     *  @return PLDM completion_code
     */
    int setNumericEffecter(uint16_t entityInstance,
                           const pldm::utils::PropertyValue& propertyValue);

    /** @brief monitor the dump
     *
     *  @param[in] object_path - The object path of the dump to monitor
     *
     */
    void monitorDump(const std::string& obj_path);

    /*keeps track how many times setEventReceiver is sent */
    void countSetEventReceiver()
    {
        setEventReceiverCnt++;
    }

    /* disables watchdog if running and Host is up */
    void checkAndDisableWatchDog();

    /** @brief To check if the watchdog app is running
     *
     *  @return the running status of watchdog app
     */
    bool watchDogRunning();

    /** @brief Method to reset the Watchdog timer on receiving platform Event
     *  Message for heartbeat elapsed time from Hostboot
     */
    void resetWatchDogTimer();

    /** @brief To disable to the watchdog timer on host poweron completion*/
    void disableWatchDogTimer();

    /** @brief to check the BMC state*/
    int checkBMCState();

    /** @brief update the dbus object paths */
    void upadteOemDbusPaths(std::string& dbusPath);
    /** @brief update the conatiner ID */
    void updateContainerID();

    void setHostEffecterState(bool status);
    /** @brief Method to perform actions when PLDM_RECORDS_MODIFIED event
     *  is received from host
     *  @param[in] entityType - entity type
     *  @param[in] stateSetId - state set id
     */
    void modifyPDROemActions(uint16_t entityType, uint16_t stateSetId);

    /** @brief D-Bus Method call to call the Panel D-Bus API
     *
     * @param[in] service - Service name for the D-Bus method
     * @param[in] objPath - The D-Bus object path
     * @param[in] dbusMethod - The Method name to be invoked
     * @param[in] dbusInterface - The D-Bus interface
     * @param[in] value - The value to be passed as argument
     *            to D-Bus method
     */
    void setBitmapMethodCall(const char* service, const char* objPath,
                             const char* dbusMethod, const char* dbusInterface,
                             const PropertyValue& value);
    std::filesystem::path getConfigDir();

    /** @brief To handle the boot types bios attributes at power on*/
    void handleBootTypesAtPowerOn();

    /** @brief To set the boot types bios attributes based on the RestartCause
     *  of host
     *
     *  @param[in] RestartCause - Host restart cause
     */
    void setBootTypesBiosAttr(const std::string& restartCause);

    /** @brief To handle the boot types bios attributes at shutdown*/
    void handleBootTypesAtChassisOff();

    /** @brief To turn off Real SAI effecter*/
    void turnOffRealSAIEffecter();

    /** @brief Fetch Real SAI status based on the partition SAI and platform SAI
     *  sensor state. Real SAI is turned on if any of the partition or platform
     *  SAI turned on else Real SAI is turned off.
     *  @return Real SAI sensor state PLDM_SENSOR_WARNING/PLDM_SENSOR_NORMAL
     */
    uint8_t fetchRealSAIStatus();

    /** @brief To process the graceful shutdown, cycle chassis power, and boot
     *  the host back up*/
    void processPowerCycleOffSoftGraceful();

    /** @brief To process powering down the host*/
    void processPowerOffSoftGraceful();

    /** @brief To process auto power restore policy*/
    void processPowerOffHardGraceful();

    /** @brief Method to reset or stop the surveillance timer
     *
     *  @param[in] value - true or false, to indicate if the timer
     *                     should be reset or turned off*/
    void startStopTimer(bool value);

    /** @brief Method to Enable/Disable timer to see if host sends the
     * surveillance ping and logs informational error if host fails to send the
     * surveillance pings
     *
     * @param[in] tid - TID of the host
     * @param[in] value - true or false, to indicate if the timer is
     *                    running or not*/
    void setSurvTimer(uint8_t tid, bool value);

    /** @brief method to fetch the properties changed
     *
     *  @param[in] chProperties - list of properties which have changed
     *  @param[in] objPath - path on which property is changed
     */
    void propertyChanged(const DbusChangedProps& chProperties,
                         std::string objPath);

    /** @brief method to trigger host effecter
     *
     *  @param[in] value - value to set
     *  @param[in] path - path for which the effecter is set
     */
    void triggerHostEffecter(bool value, std::string path);

    ~Handler() = default;

    pldm::responder::CodeUpdate* codeUpdate; //!< pointer to CodeUpdate object

    pldm::responder::SlotHandler* slotHandler;

    pldm::responder::platform::Handler*
        platformHandler; //!< pointer to PLDM platform handler

    /** @brief fd of MCTP communications socket */
    int mctp_fd;

    /** @brief MCTP EID of host firmware */
    uint8_t mctp_eid;

    /** @brief reference to an InstanceIdDb object, used to obtain a PLDM
     * instance id. */
    pldm::InstanceIdDb& instanceIdDb;
    /** @brief sdeventplus event source */
    std::unique_ptr<sdeventplus::source::Defer> assembleImageEvent;
    std::unique_ptr<sdeventplus::source::Defer> startUpdateEvent;
    std::unique_ptr<sdeventplus::source::Defer> systemRebootEvent;

    /** @brief Effecterid to dbus object path map
     */
    std::unordered_map<uint16_t, std::string> effecterIdToDbusMap;
    /** unique pointer to the SBE dump match */
    std::unique_ptr<sdbusplus::bus::match::match> sbeDumpMatch;

    /** @brief reference of main event loop of pldmd, primarily used to schedule
     *  work
     */
    sdeventplus::Event& event;

  private:
    /**@ brief system type/model */
    std::string systemType;

    /*@brief Host restart cause*/
    std::string restartCause;

    /** @brief D-Bus property changed signal match for CurrentPowerState*/
    std::unique_ptr<sdbusplus::bus::match::match> chassisOffMatch;

    const pldm_pdr* pdrRepo;

    /** @brief PLDM request handler */
    pldm::requester::Handler<pldm::requester::Request>* handler;

    /** @brief Pointer to BMC's entity association tree */
    pldm_entity_association_tree* bmcEntityTree;

    /** @brief Pointer to host effecter parser */
    pldm::host_effecters::HostEffecterParser* hostEffecterParser;

    /** @brief D-Bus property changed signal match */
    std::unique_ptr<sdbusplus::bus::match::match> hostOffMatch;
    std::unique_ptr<sdbusplus::bus::match::match> updateBIOSMatch;
    /** @brief D-Bus Interfaced added signal match for Entity Manager */
    std::unique_ptr<sdbusplus::bus::match::match> ibmCompatibleMatch;
    /** @brief D-Bus Interfaced added signal match for State Manager */
    std::unique_ptr<sdbusplus::bus::match::match> stateManagerMatch;
    /** @brief D-Bus property Changed Signal match for bootProgress*/
    std::unique_ptr<sdbusplus::bus::match::match> bootProgressMatch;
    /** @brief Timer used for monitoring surveillance pings from host */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> timer;

    /** @brief vector of DBus property changed signal match for linkReset*/
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;

    bool hostOff = true;

    bool hostTransitioningToOff = true;

    int setEventReceiverCnt = 0;

    /** @brief instanceMap is a lookup data structure to lookup <EffecterID,
     * InstanceInfo> */
    HostEffecterInstanceMap instanceMap;

    bool update = true; // to indicate if update is done

    /** @brief method to create matches for the proeprty changed signal for link
     * reset on all slot paths */
    void createMatches();
};

/** @brief Method to encode code update event msg
 *  @param[in] eventType - type of event
 *  @param[in] eventDataVec - vector of event data to be sent to host
 *  @param[in/out] requestMsg - request msg to be encoded
 *  @param[in] instanceId - instance ID
 *  @return PLDM status code
 */
int encodeEventMsg(uint8_t eventType, const std::vector<uint8_t>& eventDataVec,
                   std::vector<uint8_t>& requestMsg, uint8_t instanceId);

// HostEffecterInstanceMap instanceMap;

/** @brief method to call a DBus method
 *
 *  @param[in] entityInstance - entity instance
 *  @param[in] value - value to be set
 *
 *  @return PLDM status code
 */
int setNumericEffecter(uint16_t entityInstance,
                       const pldm::utils::PropertyValue& value);

/* @brief method to get the processor object paths*/
std::vector<std::string> getProcObjectPaths();

} // namespace oem_ibm_platform

} // namespace responder

} // namespace pldm
