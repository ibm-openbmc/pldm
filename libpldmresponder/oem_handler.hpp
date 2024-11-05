#pragma once

#include "common/types.hpp"
#include "common/utils.hpp"
#include "libpldmresponder/pdr_utils.hpp"
#include "pldmd/handler.hpp"

namespace pldm
{
namespace responder
{
namespace oem_platform
{
class Handler : public CmdHandler
{
  public:
    Handler(const pldm::utils::DBusHandler* dBusIntf) : dBusIntf(dBusIntf) {}

    /** @brief Interface to set the numeric effecter requested by pldm
     *  requester for OEM types. Each individual oem type should implement
     *  it's own handler.
     *
     *  @param[in] entityType - entity type corresponding to the effecter id
     *  @param[in] entityInstance - entity instance
     *  @param[in] effecterSemanticId - effecter semantic id
     *  @param[in] effecterDataSize - effecter value size.
     *  @param[in] effecterValue - effecter value.
     *  @param[in] effecterOffset - offset of the effecter.
     *  @param[in] effecterResolution - resolution of the effecter.
     *  @param[in] effecterId - Effecter ID sent by the requester to act on.
     *
     *  @return - Success or failure in setting the states.Returns failure in
     *            terms of PLDM completion codes if atleast one state fails to
     *            be set
     *
     */
    virtual int oemSetNumericEffecterValueHandler(
        uint16_t entityType, uint16_t entityInstance,
        uint16_t effecterSemanticId, uint8_t effecterDataSize,
        uint8_t* effecterValue, real32_t effecterOffset,
        real32_t effecterResolution, uint16_t effecterId) = 0;

    /** @brief Interface to get the state sensor readings requested by pldm
     *  requester for OEM types. Each specific type should implement a handler
     *  of it's own
     *
     *  @param[in] entityType - entity type corresponding to the sensor
     *  @param[in] entityInstance - entity instance number
     *  @param[in] entityContainerID - container id
     *  @param[in] stateSetId - state set id
     *  @param[in] compSensorCnt - composite sensor count
     *  @param[in] sensorId - sensor id
     *  @param[out] stateField - The state field data for each of the states,
     *                           equal to composite sensor count in number
     *
     *  @return - Success or failure in getting the states. Returns failure in
     *            terms of PLDM completion codes if fetching atleast one state
     *            fails
     */
    virtual int getOemStateSensorReadingsHandler(
        pldm::pdr::EntityType entityType,
        pldm::pdr::EntityInstance entityInstance,
        pldm::pdr::ContainerID entityContainerId,
        pldm::pdr::StateSetId stateSetId,
        pldm::pdr::CompositeCount compSensorCnt, uint16_t sensorId,
        std::vector<get_sensor_state_field>& stateField) = 0;

    /** @brief Interface to set the effecter requested by pldm requester
     *         for OEM types. Each individual oem type should implement
     *         it's own handler.
     *
     *  @param[in] entityType - entity type corresponding to the effecter id
     *  @param[in] stateSetId - state set id
     *  @param[in] compEffecterCnt - composite effecter count
     *  @param[in] stateField - The state field data for each of the states,
     *                         equal to compEffecterCnt in number
     *  @param[in] effecterId - Effecter id
     *
     *  @return - Success or failure in setting the states.Returns failure in
     *            terms of PLDM completion codes if atleast one state fails to
     *            be set
     */
    virtual int oemSetStateEffecterStatesHandler(
        uint16_t entityType, uint16_t stateSetId, uint8_t compEffecterCnt,
        std::vector<set_effecter_state_field>& stateField,
        uint16_t effecterId) = 0;

    /** @brief Interface to generate the OEM PDRs
     *
     * @param[in] repo - instance of concrete implementation of Repo
     */
    virtual void buildOEMPDR(pldm::responder::pdr_utils::Repo& repo) = 0;

    /** @brief Interface to check if setEventReceiver is sent to host already.
     *         If sent then then disableWatchDogTimer() would be called to
     *         disable the watchdog timer */
    virtual void checkAndDisableWatchDog() = 0;

    /** @brief Interface to check if the watchdog timer is running
     *
     * @return - true if watchdog is running, false otherwise
     * */
    virtual bool watchDogRunning() = 0;

    /** @brief Interface to reset the watchdog timer */
    virtual void resetWatchDogTimer() = 0;

    /** @brief Interface to disable the watchdog timer */
    virtual void disableWatchDogTimer() = 0;

    /** @brief Interface to keep track of how many times setEventReceiver
     *         is sent to host */
    virtual void countSetEventReceiver() = 0;

    /** @brief Interface to check the BMC state */
    virtual int checkBMCState() = 0;

    /** @brief update the dbus object paths */
    virtual void updateOemDbusPaths(std::string& dbusPath) = 0;

    /** @brief Interface to update container ID */
    virtual void updateContainerID() = 0;

    /** @brief Interface to set the host effecter state
     *  @param status - the status of dump creation
     *  @param entityTypeReceived - the entity type
     *  @param entityInstance - entity instance number
     *
     */
    virtual void setHostEffecterState(bool status, uint16_t entityTypeReceived,
                                      uint16_t entityInstance) = 0;

    /** @brief Interface to fetch the last BMC record from the PDR repository
     *
     *  @param[in] repo - pointer to BMC's primary PDR repo
     *
     *  @return the last BMC record from the repo
     */
    virtual const pldm_pdr_record* fetchLastBMCRecord(const pldm_pdr* repo) = 0;

    /** @brief Interface to check if the record handle passed is in remote PDR
     *         record handle range
     *
     *  @param[in] record_handle - record handle of the PDR
     *
     *  @return true if record handle passed is in host PDR record handle range
     */
    virtual bool checkRecordHandleInRange(const uint32_t& record_handle) = 0;

    /** @brief Interface to the process setEventReceiver*/
    virtual void processSetEventReceiver() = 0;

    /** @brief Interface to monitor the surveillance pings from remote terminus
     *  @param[in] value - true or false, to indicate if the timer
     *                     should be reset or turned off*/
    virtual void startStopTimer(bool value) = 0;

    /** @brief Interface to monitor the surveillance pings from remote terminus
     *
     * @param[in] tid - TID of the remote terminus
     * @param[in] value - true or false, to indicate if the timer is
     *                   running or not
     * */
    virtual void setSurvTimer(uint8_t tid, bool value) = 0;

    /** @brief Interface to perform OEM actions*/
    virtual void modifyPDROemActions(uint16_t entityType,
                                     uint16_t stateSetId) = 0;

    /** @brief To handle the boot types bios attributes at power on*/
    virtual void handleBootTypesAtPowerOn() = 0;

    /** @brief To handle the boot types bios attributes at shutdown*/
    virtual void handleBootTypesAtChassisOff() = 0;

    virtual ~Handler() = default;

  protected:
    const pldm::utils::DBusHandler* dBusIntf;
};

} // namespace oem_platform

namespace oem_fru
{

class Handler : public CmdHandler
{
  public:
    Handler() {}

    /** @brief Process OEM FRU record
     *
     * @param[in] fruData - the data of the fru
     *
     * @return success or failure
     */
    virtual int processOEMFRUTable(const std::vector<uint8_t>& fruData) = 0;

    virtual ~Handler() = default;
};

} // namespace oem_fru

namespace oem_utils
{
using namespace pldm::utils;

class Handler : public CmdHandler
{
  public:
    Handler(const pldm::utils::DBusHandler* dBusIntf) : dBusIntf(dBusIntf) {}

    /** @brief Collecting core count data and setting to Dbus properties
     *
     *  @param[in] associations - the data of entity association
     *  @param[in] entityMaps - the mapping of entity to DBus string
     *
     *  @return int - the processor core count
     */
    virtual int setCoreCount(const EntityAssociations& associations,
                             const EntityMaps entityMaps) = 0;

    /** @brief checks if a pcie adapter is IBM specific
     *         cable card
     *  @param[in] objPath - FRU object path
     *
     *  @return bool - true if IBM specific card
     */
    virtual bool checkModelPresence(const std::string& objPath) = 0;

    /** @brief checks whether the fru is actually present
     *  @param[in] objPath - the fru object path
     *
     *  @return bool to indicate presence or absence
     */
    virtual bool checkFruPresence(const char* objPath) = 0;

    /** @brief finds the ports under an adapter
     *
     *  @param[in] adapterObjPath - D-Bus object path for the adapter
     *
     *  @return std::vector<std::string> - port object paths
     */
    virtual std::vector<std::string>
        findPortObjects(const std::string& adapterObjPath) = 0;

    virtual ~Handler() = default;

  protected:
    const pldm::utils::DBusHandler* dBusIntf;
};

} // namespace oem_utils

namespace oem_fileio
{

class Handler : public CmdHandler
{
  public:
    /**
     *
     * @brief Interface to send new chapdata file available request to
     *  the oem layer where chapdata contain encrypted key and keyname
     *
     *  @param[in] chapNameStr - unique chapname associated with each user
     * challenge
     *
     *  @param[in] userChallengeStr - encrypted user challenge to authenticate
     * server
     *
     */
    virtual void
        newChapDataFileAvailable(const std::string& chapNameStr,
                                 const std::string& userChallengeStr) = 0;

    virtual ~Handler() = default;
};

} // namespace oem_fileio

} // namespace responder

} // namespace pldm
