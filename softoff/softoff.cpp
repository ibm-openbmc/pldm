#include "softoff.hpp"

#include "libpldm/entity.h"
#include "libpldm/platform.h"
#include "libpldm/pldm.h"
#include "libpldm/state_set.h"

#include "common/utils.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/time.hpp>

#include <array>
#include <iostream>

PHOSPHOR_LOG2_USING;

namespace pldm
{
using namespace sdeventplus;
using namespace sdeventplus::source;
constexpr auto clockId = sdeventplus::ClockId::RealTime;
using Clock = Clock<clockId>;
using Timer = Time<clockId>;

using sdbusplus::exception::SdBusError;

// Shutdown effecter terminus ID, set when we look up the effecter
pldm::pdr::TerminusID TID;

const pldm::pdr::TerminusID hypervisor_TID = 208;

namespace sdbusRule = sdbusplus::bus::match::rules;

SoftPowerOff::SoftPowerOff(sdbusplus::bus_t& bus, sd_event* event,
                           bool noTimeOut) :
    bus(bus),
    timer(event), noTimeOut(noTimeOut)
{
    getHostState();
    if (hasError || completed)
    {
        return;
    }

    auto rc = getEffecterID();
    if (completed)
    {
        error("pldm-softpoweroff: effecter to initiate softoff not found");
        return;
    }
    else if (rc != PLDM_SUCCESS)
    {
        hasError = true;
        return;
    }

    rc = getSensorInfo();
    if (rc != PLDM_SUCCESS)
    {
        error("Message get Sensor PDRs error. PLDM error code = {RC}", "RC",
              lg2::hex, (int)rc);
        hasError = true;
        return;
    }

    // Matches on the pldm StateSensorEvent signal
    pldmEventSignal = std::make_unique<sdbusplus::bus::match_t>(
        bus,
        sdbusRule::type::signal() + sdbusRule::member("StateSensorEvent") +
            sdbusRule::path("/xyz/openbmc_project/pldm") +
            sdbusRule::interface("xyz.openbmc_project.PLDM.Event"),
        std::bind(std::mem_fn(&SoftPowerOff::hostSoftOffComplete), this,
                  std::placeholders::_1));
}

int SoftPowerOff::getHostState()
{
    try
    {
        pldm::utils::PropertyValue propertyValue =
            pldm::utils::DBusHandler().getDbusPropertyVariant(
                "/xyz/openbmc_project/state/host0", "CurrentHostState",
                "xyz.openbmc_project.State.Host");

        if ((std::get<std::string>(propertyValue) !=
             "xyz.openbmc_project.State.Host.HostState.Running") &&
            (std::get<std::string>(propertyValue) !=
             "xyz.openbmc_project.State.Host.HostState.TransitioningToOff"))
        {
            // Host state is not "Running", this app should return success
            completed = true;
            return PLDM_SUCCESS;
        }
    }
    catch (const std::exception& e)
    {
        error("PLDM host soft off: Can't get current host state.");
        hasError = true;
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

void SoftPowerOff::hostSoftOffComplete(sdbusplus::message_t& msg)
{
    pldm::pdr::TerminusID msgTID;
    pldm::pdr::SensorID msgSensorID;
    pldm::pdr::SensorOffset msgSensorOffset;
    pldm::pdr::EventState msgEventState;
    pldm::pdr::EventState msgPreviousEventState;

    // Read the msg and populate each variable
    msg.read(msgTID, msgSensorID, msgSensorOffset, msgEventState,
             msgPreviousEventState);

    if (msgSensorID == sensorID && msgSensorOffset == sensorOffset &&
        msgEventState == PLDM_SW_TERM_GRACEFUL_SHUTDOWN && msgTID == TID)
    {
        info("Recieved the graceful shutdown signal from host");
        if (!noTimeOut)
        {
            // Receive Graceful shutdown completion event message. Disable the
            // timer
            auto rc = timer.stop();
            if (rc < 0)
            {
                error("PLDM soft off: Failure to STOP the timer. ERRNO={RC}",
                      "RC", rc);
            }
        }

        // This marks the completion of pldm soft power off.
        completed = true;
    }
}

int SoftPowerOff::getEffecterID()
{
    auto& bus = pldm::utils::DBusHandler::getBus();

    // VMM is a logical entity, so the bit 15 in entity type is set.
    pdr::EntityType entityType = PLDM_ENTITY_VIRTUAL_MACHINE_MANAGER | 0x8000;

    try
    {
        std::vector<std::vector<uint8_t>> VMMResponse{};
        auto VMMMethod = bus.new_method_call(
            "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
            "xyz.openbmc_project.PLDM.PDR", "FindStateEffecterPDR");
        VMMMethod.append(TID, entityType,
                         (uint16_t)PLDM_STATE_SET_SW_TERMINATION_STATUS);

        auto VMMResponseMsg = bus.call(VMMMethod, dbusTimeout);

        VMMResponseMsg.read(VMMResponse);
        if (VMMResponse.size() != 0)
        {
            for (auto& rep : VMMResponse)
            {
                auto VMMPdr =
                    reinterpret_cast<pldm_state_effecter_pdr*>(rep.data());
                effecterID = VMMPdr->effecter_id;
                TID = hypervisor_TID;
            }
        }
        else
        {
            VMMPdrExist = false;
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("PLDM soft off: Error get VMM PDR,ERROR={ERR_EXCEP}", "ERR_EXCEP",
              e.what());
        VMMPdrExist = false;
    }

    if (VMMPdrExist)
    {
        return PLDM_SUCCESS;
    }

    // If the Virtual Machine Manager PDRs doesn't exist, go find the System
    // Chassis PDRs.
    // The host firmware may attach the graceful shutdown effecter to this
    // entity.
    entityType = PLDM_ENTITY_SYSTEM_CHASSIS;
    try
    {
        std::vector<std::vector<uint8_t>> sysFwResponse{};
        auto sysFwMethod = bus.new_method_call(
            "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
            "xyz.openbmc_project.PLDM.PDR", "FindStateEffecterPDR");
        sysFwMethod.append(TID, entityType,
                           (uint16_t)PLDM_STATE_SET_SW_TERMINATION_STATUS);

        auto sysFwResponseMsg = bus.call(sysFwMethod, dbusTimeout);

        sysFwResponseMsg.read(sysFwResponse);

        if (sysFwResponse.size() == 0)
        {
            error("No effecter ID has been found that matches the criteria");
            return PLDM_ERROR;
        }

        for (auto& rep : sysFwResponse)
        {
            auto sysFwPdr =
                reinterpret_cast<pldm_state_effecter_pdr*>(rep.data());
            effecterID = sysFwPdr->effecter_id;
            TID = sysFwPdr->terminus_handle;
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("PLDM soft off: Error get system firmware PDR,ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        completed = true;
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int SoftPowerOff::getSensorInfo()
{
    pldm::pdr::EntityType entityType;

    entityType = VMMPdrExist ? PLDM_ENTITY_VIRTUAL_MACHINE_MANAGER | 0x8000
                             : PLDM_ENTITY_SYSTEM_CHASSIS;

    try
    {
        auto& bus = pldm::utils::DBusHandler::getBus();
        std::vector<std::vector<uint8_t>> Response{};
        auto method = bus.new_method_call(
            "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
            "xyz.openbmc_project.PLDM.PDR", "FindStateSensorPDR");
        method.append(TID, entityType,
                      (uint16_t)PLDM_STATE_SET_SW_TERMINATION_STATUS);

        auto ResponseMsg = bus.call(method, dbusTimeout);

        ResponseMsg.read(Response);

        if (Response.size() == 0)
        {
            error("No sensor PDR has been found that matches the criteria");
            return PLDM_ERROR;
        }

        pldm_state_sensor_pdr* pdr;
        for (auto& rep : Response)
        {
            pdr = reinterpret_cast<pldm_state_sensor_pdr*>(rep.data());
            if (!pdr)
            {
                error("Failed to get state sensor PDR.");
                return PLDM_ERROR;
            }
        }

        sensorID = pdr->sensor_id;

        auto compositeSensorCount = pdr->composite_sensor_count;
        auto possibleStatesStart = pdr->possible_states;

        for (auto offset = 0; offset < compositeSensorCount; offset++)
        {
            auto possibleStates =
                reinterpret_cast<state_sensor_possible_states*>(
                    possibleStatesStart);
            auto setId = possibleStates->state_set_id;
            auto possibleStateSize = possibleStates->possible_states_size;

            if (setId == PLDM_STATE_SET_SW_TERMINATION_STATUS)
            {
                sensorOffset = offset;
                break;
            }
            possibleStatesStart += possibleStateSize + sizeof(setId) +
                                   sizeof(possibleStateSize);
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("PLDM soft off: Error get State Sensor PDR,ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }

    return PLDM_SUCCESS;
}

int SoftPowerOff::hostSoftOff(sdeventplus::Event& event)
{
    constexpr uint8_t effecterCount = 1;
    uint8_t mctpEID;
    uint8_t instanceID;

    mctpEID = pldm::utils::readHostEID();

    uint8_t effecterState;
    auto requestHostTransition =
        pldm::utils::DBusHandler().getDbusProperty<std::string>(
            "/xyz/openbmc_project/state/host0", "RequestedHostTransition",
            "xyz.openbmc_project.State.Host");
    if (requestHostTransition !=
        "xyz.openbmc_project.State.Host.Transition.Off")
    {
        effecterState = PLDM_SW_TERM_GRACEFUL_RESTART_REQUESTED;
    }
    else
    {
        effecterState = PLDM_SW_TERM_GRACEFUL_SHUTDOWN_REQUESTED;
    }

    // Get instanceID
    try
    {
        auto& bus = pldm::utils::DBusHandler::getBus();
        auto method = bus.new_method_call(
            "xyz.openbmc_project.PLDM", "/xyz/openbmc_project/pldm",
            "xyz.openbmc_project.PLDM.Requester", "GetInstanceId");
        method.append(mctpEID);

        auto ResponseMsg = bus.call(method, dbusTimeout);

        ResponseMsg.read(instanceID);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error("PLDM soft off: Error get instanceID,ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }

    std::array<uint8_t, sizeof(pldm_msg_hdr) + sizeof(effecterID) +
                            sizeof(effecterCount) +
                            sizeof(set_effecter_state_field)>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    set_effecter_state_field stateField{PLDM_REQUEST_SET, effecterState};
    auto rc = encode_set_state_effecter_states_req(
        instanceID, effecterID, effecterCount, &stateField, request);
    if (rc != PLDM_SUCCESS)
    {
        error("Message encode failure. PLDM error code = {RC}", "RC", lg2::hex,
              static_cast<int>(rc));
        return PLDM_ERROR;
    }

    // Open connection to MCTP socket
    int fd = pldm_open();
    if (-1 == fd)
    {
        error("Failed to connect to mctp demux daemon");
        return PLDM_ERROR;
    }

    // Add a timer to the event loop, default 30s.
    auto timerCallback =
        [=, this](Timer& /*source*/, Timer::TimePoint /*time*/) {
        if (!responseReceived)
        {
            error(
                "PLDM soft off: ERROR! Can't get the response for the PLDM request msg. Time out! Exit the pldm-softpoweroff");
            exit(-1);
        }
        return;
    };
    Timer time(event, (Clock(event).now() + std::chrono::seconds{30}),
               std::chrono::seconds{1}, std::move(timerCallback));

    // Add a callback to handle EPOLLIN on fd
    auto callback = [=, this](IO& io, int fd, uint32_t revents) {
        if (!(revents & EPOLLIN))
        {
            return;
        }

        uint8_t* responseMsg = nullptr;
        size_t responseMsgSize{};

        auto rc = pldm_recv(mctpEID, fd, request->hdr.instance_id, &responseMsg,
                            &responseMsgSize);
        if (rc)
        {
            error("Soft off: failed to recv pldm data. PLDM RC = {RC}", "RC",
                  static_cast<int>(rc));
            return;
        }

        std::unique_ptr<uint8_t, decltype(std::free)*> responseMsgPtr{
            responseMsg, std::free};

        // We've got the response meant for the PLDM request msg that was
        // sent out
        io.set_enabled(Enabled::Off);
        auto response = reinterpret_cast<pldm_msg*>(responseMsgPtr.get());
        if (response->payload[0] != PLDM_SUCCESS)
        {
            error(
                "Got a bad respose for soft off seteffecter states command . PLDM RC = {RC}",
                "RC", static_cast<uint16_t>(response->payload[0]));
            exit(-1);
        }

        error(
            "Got the response for set effecter states command, PLDM RC = {RC}",
            "RC", lg2::hex, static_cast<uint16_t>(response->payload[0]));
        std::vector<uint8_t> responseMessage;
        responseMessage.resize(responseMsgSize);
        memcpy(responseMessage.data(), responseMsg, responseMessage.size());
        pldm::utils::printBuffer(pldm::utils::Rx, responseMessage);

        responseReceived = true;

        if (!noTimeOut)
        {
            // Start Timer
            using namespace std::chrono;
            auto timeMicroseconds =
                duration_cast<microseconds>(seconds(SOFTOFF_TIMEOUT_SECONDS));

            auto ret = startTimer(timeMicroseconds);
            if (ret < 0)
            {
                error(
                    "Failure to start Host soft off wait timer, ERRNO = {ERR} Exit the pldm-softpoweroff",
                    "ERR", ret);
                exit(-1);
            }
            else
            {
                error(
                    "Timer started waiting for host soft off, TIMEOUT_IN_SEC = {TIMEOUT_IN_SEC}",
                    "TIMEOUT_IN_SEC", SOFTOFF_TIMEOUT_SECONDS);
            }
        }
        return;
    };
    IO io(event, fd, EPOLLIN, std::move(callback));

    // Send PLDM Request message - pldm_send doesn't wait for response
    rc = pldm_send(mctpEID, fd, requestMsg.data(), requestMsg.size());
    if (0 > rc)
    {
        error(
            "Failed to send message/receive response. RC = {RC}, errno = {ERR}",
            "RC", static_cast<int>(rc), "ERR", errno);
        return PLDM_ERROR;
    }
    std::vector<uint8_t> requestbuffer(requestMsg.begin(), requestMsg.end());
    pldm::utils::printBuffer(pldm::utils::Tx, requestbuffer);

    // Time out or soft off complete
    while (!isCompleted() && !isTimerExpired())
    {
        try
        {
            event.run(std::nullopt);
        }
        catch (const sdeventplus::SdEventError& e)
        {
            error(
                "PLDM host soft off: Failure in processing request.ERROR= {ERR_EXCEP}",
                "ERR_EXCEP", e.what());
            return PLDM_ERROR;
        }
    }

    return PLDM_SUCCESS;
}

int SoftPowerOff::startTimer(const std::chrono::microseconds& usec)
{
    return timer.start(usec);
}
} // namespace pldm
